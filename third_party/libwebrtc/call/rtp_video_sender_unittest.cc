/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "call/rtp_video_sender.h"

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/test/mock_frame_transformer.h"
#include "call/rtp_transport_controller_send.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "modules/rtp_rtcp/source/rtcp_packet/nack.h"
#include "modules/rtp_rtcp/source/rtp_dependency_descriptor_extension.h"
#include "modules/rtp_rtcp/source/rtp_packet.h"
#include "modules/video_coding/fec_controller_default.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "rtc_base/rate_limiter.h"
#include "test/explicit_key_value_config.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/mock_transport.h"
#include "test/scenario/scenario.h"
#include "test/scoped_key_value_config.h"
#include "test/time_controller/simulated_time_controller.h"
#include "video/send_statistics_proxy.h"

namespace webrtc {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::SaveArg;
using ::testing::SizeIs;

const int8_t kPayloadType = 96;
const uint32_t kSsrc1 = 12345;
const uint32_t kSsrc2 = 23456;
const uint32_t kRtxSsrc1 = 34567;
const uint32_t kRtxSsrc2 = 45678;
const int16_t kInitialPictureId1 = 222;
const int16_t kInitialPictureId2 = 44;
const int16_t kInitialTl0PicIdx1 = 99;
const int16_t kInitialTl0PicIdx2 = 199;
const int64_t kRetransmitWindowSizeMs = 500;
const int kTransportsSequenceExtensionId = 7;
const int kDependencyDescriptorExtensionId = 8;

class MockRtcpIntraFrameObserver : public RtcpIntraFrameObserver {
 public:
  MOCK_METHOD(void, OnReceivedIntraFrameRequest, (uint32_t), (override));
};

RtpSenderObservers CreateObservers(
    RtcpIntraFrameObserver* intra_frame_callback,
    ReportBlockDataObserver* report_block_data_observer,
    StreamDataCountersCallback* rtp_stats,
    BitrateStatisticsObserver* bitrate_observer,
    FrameCountObserver* frame_count_observer,
    RtcpPacketTypeCounterObserver* rtcp_type_observer) {
  RtpSenderObservers observers;
  observers.rtcp_rtt_stats = nullptr;
  observers.intra_frame_callback = intra_frame_callback;
  observers.rtcp_loss_notification_observer = nullptr;
  observers.report_block_data_observer = report_block_data_observer;
  observers.rtp_stats = rtp_stats;
  observers.bitrate_observer = bitrate_observer;
  observers.frame_count_observer = frame_count_observer;
  observers.rtcp_type_observer = rtcp_type_observer;
  observers.send_packet_observer = nullptr;
  return observers;
}

BitrateConstraints GetBitrateConfig() {
  BitrateConstraints bitrate_config;
  bitrate_config.min_bitrate_bps = 30000;
  bitrate_config.start_bitrate_bps = 300000;
  bitrate_config.max_bitrate_bps = 3000000;
  return bitrate_config;
}

VideoSendStream::Config CreateVideoSendStreamConfig(
    Transport* transport,
    const std::vector<uint32_t>& ssrcs,
    const std::vector<uint32_t>& rtx_ssrcs,
    int payload_type) {
  VideoSendStream::Config config(transport);
  config.rtp.ssrcs = ssrcs;
  config.rtp.rtx.ssrcs = rtx_ssrcs;
  config.rtp.payload_type = payload_type;
  config.rtp.rtx.payload_type = payload_type + 1;
  config.rtp.nack.rtp_history_ms = 1000;
  config.rtp.extensions.emplace_back(RtpExtension::kTransportSequenceNumberUri,
                                     kTransportsSequenceExtensionId);
  config.rtp.extensions.emplace_back(RtpDependencyDescriptorExtension::Uri(),
                                     kDependencyDescriptorExtensionId);
  config.rtp.extmap_allow_mixed = true;
  return config;
}

class RtpVideoSenderTestFixture {
 public:
  RtpVideoSenderTestFixture(
      const std::vector<uint32_t>& ssrcs,
      const std::vector<uint32_t>& rtx_ssrcs,
      int payload_type,
      const std::map<uint32_t, RtpPayloadState>& suspended_payload_states,
      FrameCountObserver* frame_count_observer,
      rtc::scoped_refptr<FrameTransformerInterface> frame_transformer,
      const FieldTrialsView* field_trials = nullptr)
      : time_controller_(Timestamp::Millis(1000000)),
        env_(CreateEnvironment(&field_trials_,
                               field_trials,
                               time_controller_.GetClock(),
                               time_controller_.CreateTaskQueueFactory())),
        config_(CreateVideoSendStreamConfig(&transport_,
                                            ssrcs,
                                            rtx_ssrcs,
                                            payload_type)),
        bitrate_config_(GetBitrateConfig()),
        transport_controller_(
            RtpTransportConfig{.env = env_, .bitrate_config = bitrate_config_}),
        stats_proxy_(time_controller_.GetClock(),
                     config_,
                     VideoEncoderConfig::ContentType::kRealtimeVideo,
                     env_.field_trials()),
        retransmission_rate_limiter_(time_controller_.GetClock(),
                                     kRetransmitWindowSizeMs) {
    transport_controller_.EnsureStarted();
    std::map<uint32_t, RtpState> suspended_ssrcs;
    router_ = std::make_unique<RtpVideoSender>(
        time_controller_.GetClock(), suspended_ssrcs, suspended_payload_states,
        config_.rtp, config_.rtcp_report_interval_ms, &transport_,
        CreateObservers(&encoder_feedback_, &stats_proxy_, &stats_proxy_,
                        &stats_proxy_, frame_count_observer, &stats_proxy_),
        &transport_controller_, &env_.event_log(),
        &retransmission_rate_limiter_,
        std::make_unique<FecControllerDefault>(env_), nullptr, CryptoOptions{},
        frame_transformer, env_.field_trials(),
        time_controller_.GetTaskQueueFactory());
  }

  RtpVideoSenderTestFixture(
      const std::vector<uint32_t>& ssrcs,
      const std::vector<uint32_t>& rtx_ssrcs,
      int payload_type,
      const std::map<uint32_t, RtpPayloadState>& suspended_payload_states,
      FrameCountObserver* frame_count_observer,
      const FieldTrialsView* field_trials = nullptr)
      : RtpVideoSenderTestFixture(ssrcs,
                                  rtx_ssrcs,
                                  payload_type,
                                  suspended_payload_states,
                                  frame_count_observer,
                                  /*frame_transformer=*/nullptr,
                                  field_trials) {}

  RtpVideoSenderTestFixture(
      const std::vector<uint32_t>& ssrcs,
      const std::vector<uint32_t>& rtx_ssrcs,
      int payload_type,
      const std::map<uint32_t, RtpPayloadState>& suspended_payload_states,
      const FieldTrialsView* field_trials = nullptr)
      : RtpVideoSenderTestFixture(ssrcs,
                                  rtx_ssrcs,
                                  payload_type,
                                  suspended_payload_states,
                                  /*frame_count_observer=*/nullptr,
                                  /*frame_transformer=*/nullptr,
                                  field_trials) {}

  ~RtpVideoSenderTestFixture() { SetSending(false); }

  RtpVideoSender* router() { return router_.get(); }
  MockTransport& transport() { return transport_; }
  void AdvanceTime(TimeDelta delta) { time_controller_.AdvanceTime(delta); }

  void SetSending(bool sending) { router_->SetSending(sending); }

 private:
  test::ScopedKeyValueConfig field_trials_;
  NiceMock<MockTransport> transport_;
  NiceMock<MockRtcpIntraFrameObserver> encoder_feedback_;
  GlobalSimulatedTimeController time_controller_;
  Environment env_;
  VideoSendStream::Config config_;
  BitrateConstraints bitrate_config_;
  RtpTransportControllerSend transport_controller_;
  SendStatisticsProxy stats_proxy_;
  RateLimiter retransmission_rate_limiter_;
  std::unique_ptr<RtpVideoSender> router_;
};

BitrateAllocationUpdate CreateBitrateAllocationUpdate(int target_bitrate_bps) {
  BitrateAllocationUpdate update;
  update.target_bitrate = DataRate::BitsPerSec(target_bitrate_bps);
  update.round_trip_time = TimeDelta::Zero();
  return update;
}

}  // namespace

TEST(RtpVideoSenderTest, SendOnOneModule) {
  constexpr uint8_t kPayload = 'a';
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image._frameType = VideoFrameType::kVideoFrameKey;
  encoded_image.SetEncodedData(EncodedImageBuffer::Create(&kPayload, 1));

  RtpVideoSenderTestFixture test({kSsrc1}, {kRtxSsrc1}, kPayloadType, {});
  EXPECT_NE(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image, nullptr).error);

  test.SetSending(true);
  EXPECT_EQ(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image, nullptr).error);

  test.SetSending(false);
  EXPECT_NE(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image, nullptr).error);

  test.SetSending(true);
  EXPECT_EQ(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image, nullptr).error);
}

TEST(RtpVideoSenderTest, OnEncodedImageReturnOkWhenSendingTrue) {
  constexpr uint8_t kPayload = 'a';
  EncodedImage encoded_image_1;
  encoded_image_1.SetRtpTimestamp(1);
  encoded_image_1.capture_time_ms_ = 2;
  encoded_image_1._frameType = VideoFrameType::kVideoFrameKey;
  encoded_image_1.SetEncodedData(EncodedImageBuffer::Create(&kPayload, 1));

  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {kRtxSsrc1, kRtxSsrc2},
                                 kPayloadType, {});

  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP8;

  test.SetSending(true);
  EXPECT_EQ(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image_1, &codec_info).error);

  EncodedImage encoded_image_2(encoded_image_1);
  encoded_image_2.SetSimulcastIndex(1);
  EXPECT_EQ(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image_2, &codec_info).error);
}

TEST(RtpVideoSenderTest, OnEncodedImageReturnErrorCodeWhenSendingFalse) {
  constexpr uint8_t kPayload = 'a';
  EncodedImage encoded_image_1;
  encoded_image_1.SetRtpTimestamp(1);
  encoded_image_1.capture_time_ms_ = 2;
  encoded_image_1._frameType = VideoFrameType::kVideoFrameKey;
  encoded_image_1.SetEncodedData(EncodedImageBuffer::Create(&kPayload, 1));

  EncodedImage encoded_image_2(encoded_image_1);
  encoded_image_2.SetSimulcastIndex(1);

  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {kRtxSsrc1, kRtxSsrc2},
                                 kPayloadType, {});
  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP8;

  // Setting rtp streams to inactive will turn the payload router to
  // inactive.
  test.SetSending(false);
  // An incoming encoded image will not ask the module to send outgoing data
  // because the payload router is inactive.
  EXPECT_NE(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image_1, &codec_info).error);
  EXPECT_NE(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image_2, &codec_info).error);
}

TEST(RtpVideoSenderTest,
     DiscardsHigherSimulcastFramesAfterLayerDisabledInVideoLayersAllocation) {
  constexpr uint8_t kPayload = 'a';
  EncodedImage encoded_image_1;
  encoded_image_1.SetRtpTimestamp(1);
  encoded_image_1.capture_time_ms_ = 2;
  encoded_image_1._frameType = VideoFrameType::kVideoFrameKey;
  encoded_image_1.SetEncodedData(EncodedImageBuffer::Create(&kPayload, 1));
  EncodedImage encoded_image_2(encoded_image_1);
  encoded_image_2.SetSimulcastIndex(1);
  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP8;
  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {kRtxSsrc1, kRtxSsrc2},
                                 kPayloadType, {});
  test.SetSending(true);
  // A layer is sent on both rtp streams.
  test.router()->OnVideoLayersAllocationUpdated(
      {.active_spatial_layers = {{.rtp_stream_index = 0},
                                 {.rtp_stream_index = 1}}});

  EXPECT_EQ(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image_1, &codec_info).error);
  EXPECT_EQ(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image_2, &codec_info).error);

  // Only rtp stream index 0 is configured to send a stream.
  test.router()->OnVideoLayersAllocationUpdated(
      {.active_spatial_layers = {{.rtp_stream_index = 0}}});
  EXPECT_EQ(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image_1, &codec_info).error);
  EXPECT_NE(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image_2, &codec_info).error);
}

TEST(RtpVideoSenderTest, CreateWithNoPreviousStates) {
  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {kRtxSsrc1, kRtxSsrc2},
                                 kPayloadType, {});
  test.SetSending(true);

  std::map<uint32_t, RtpPayloadState> initial_states =
      test.router()->GetRtpPayloadStates();
  EXPECT_EQ(2u, initial_states.size());
  EXPECT_NE(initial_states.find(kSsrc1), initial_states.end());
  EXPECT_NE(initial_states.find(kSsrc2), initial_states.end());
}

TEST(RtpVideoSenderTest, CreateWithPreviousStates) {
  const int64_t kState1SharedFrameId = 123;
  const int64_t kState2SharedFrameId = 234;
  RtpPayloadState state1;
  state1.picture_id = kInitialPictureId1;
  state1.tl0_pic_idx = kInitialTl0PicIdx1;
  state1.shared_frame_id = kState1SharedFrameId;
  RtpPayloadState state2;
  state2.picture_id = kInitialPictureId2;
  state2.tl0_pic_idx = kInitialTl0PicIdx2;
  state2.shared_frame_id = kState2SharedFrameId;
  std::map<uint32_t, RtpPayloadState> states = {{kSsrc1, state1},
                                                {kSsrc2, state2}};

  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {kRtxSsrc1, kRtxSsrc2},
                                 kPayloadType, states);
  test.SetSending(true);

  std::map<uint32_t, RtpPayloadState> initial_states =
      test.router()->GetRtpPayloadStates();
  EXPECT_EQ(2u, initial_states.size());
  EXPECT_EQ(kInitialPictureId1, initial_states[kSsrc1].picture_id);
  EXPECT_EQ(kInitialTl0PicIdx1, initial_states[kSsrc1].tl0_pic_idx);
  EXPECT_EQ(kInitialPictureId2, initial_states[kSsrc2].picture_id);
  EXPECT_EQ(kInitialTl0PicIdx2, initial_states[kSsrc2].tl0_pic_idx);
  EXPECT_EQ(kState2SharedFrameId, initial_states[kSsrc1].shared_frame_id);
  EXPECT_EQ(kState2SharedFrameId, initial_states[kSsrc2].shared_frame_id);
}

TEST(RtpVideoSenderTest, FrameCountCallbacks) {
  class MockFrameCountObserver : public FrameCountObserver {
   public:
    MOCK_METHOD(void,
                FrameCountUpdated,
                (const FrameCounts& frame_counts, uint32_t ssrc),
                (override));
  } callback;

  RtpVideoSenderTestFixture test({kSsrc1}, {kRtxSsrc1}, kPayloadType, {},
                                 &callback);

  constexpr uint8_t kPayload = 'a';
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image._frameType = VideoFrameType::kVideoFrameKey;
  encoded_image.SetEncodedData(EncodedImageBuffer::Create(&kPayload, 1));

  encoded_image._frameType = VideoFrameType::kVideoFrameKey;

  // No callbacks when not active.
  EXPECT_CALL(callback, FrameCountUpdated).Times(0);
  EXPECT_NE(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image, nullptr).error);
  ::testing::Mock::VerifyAndClearExpectations(&callback);

  test.SetSending(true);

  FrameCounts frame_counts;
  EXPECT_CALL(callback, FrameCountUpdated(_, kSsrc1))
      .WillOnce(SaveArg<0>(&frame_counts));
  EXPECT_EQ(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image, nullptr).error);

  EXPECT_EQ(1, frame_counts.key_frames);
  EXPECT_EQ(0, frame_counts.delta_frames);

  ::testing::Mock::VerifyAndClearExpectations(&callback);

  encoded_image._frameType = VideoFrameType::kVideoFrameDelta;
  EXPECT_CALL(callback, FrameCountUpdated(_, kSsrc1))
      .WillOnce(SaveArg<0>(&frame_counts));
  EXPECT_EQ(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image, nullptr).error);

  EXPECT_EQ(1, frame_counts.key_frames);
  EXPECT_EQ(1, frame_counts.delta_frames);
}

// Integration test verifying that ack of packet via TransportFeedback means
// that the packet is removed from RtpPacketHistory and won't be retransmitted
// again.
TEST(RtpVideoSenderTest, DoesNotRetrasmitAckedPackets) {
  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {kRtxSsrc1, kRtxSsrc2},
                                 kPayloadType, {});
  test.SetSending(true);

  constexpr uint8_t kPayload = 'a';
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image._frameType = VideoFrameType::kVideoFrameKey;
  encoded_image.SetEncodedData(EncodedImageBuffer::Create(&kPayload, 1));

  // Send two tiny images, mapping to two RTP packets. Capture sequence numbers.
  std::vector<uint16_t> rtp_sequence_numbers;
  std::vector<uint16_t> transport_sequence_numbers;
  EXPECT_CALL(test.transport(), SendRtp)
      .Times(2)
      .WillRepeatedly([&rtp_sequence_numbers, &transport_sequence_numbers](
                          rtc::ArrayView<const uint8_t> packet,
                          const PacketOptions& options) {
        RtpPacket rtp_packet;
        EXPECT_TRUE(rtp_packet.Parse(packet));
        rtp_sequence_numbers.push_back(rtp_packet.SequenceNumber());
        transport_sequence_numbers.push_back(options.packet_id);
        return true;
      });
  EXPECT_EQ(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image, nullptr).error);
  encoded_image.SetRtpTimestamp(2);
  encoded_image.capture_time_ms_ = 3;
  EXPECT_EQ(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(encoded_image, nullptr).error);

  test.AdvanceTime(TimeDelta::Millis(33));

  // Construct a NACK message for requesting retransmission of both packet.
  rtcp::Nack nack;
  nack.SetMediaSsrc(kSsrc1);
  nack.SetPacketIds(rtp_sequence_numbers);
  rtc::Buffer nack_buffer = nack.Build();

  std::vector<uint16_t> retransmitted_rtp_sequence_numbers;
  EXPECT_CALL(test.transport(), SendRtp)
      .Times(2)
      .WillRepeatedly([&retransmitted_rtp_sequence_numbers](
                          rtc::ArrayView<const uint8_t> packet,
                          const PacketOptions& options) {
        RtpPacket rtp_packet;
        EXPECT_TRUE(rtp_packet.Parse(packet));
        EXPECT_EQ(rtp_packet.Ssrc(), kRtxSsrc1);
        // Capture the retransmitted sequence number from the RTX header.
        rtc::ArrayView<const uint8_t> payload = rtp_packet.payload();
        retransmitted_rtp_sequence_numbers.push_back(
            ByteReader<uint16_t>::ReadBigEndian(payload.data()));
        return true;
      });
  test.router()->DeliverRtcp(nack_buffer.data(), nack_buffer.size());
  test.AdvanceTime(TimeDelta::Millis(33));

  // Verify that both packets were retransmitted.
  EXPECT_EQ(retransmitted_rtp_sequence_numbers, rtp_sequence_numbers);

  // Simulate transport feedback indicating fist packet received, next packet
  // lost (not other way around as that would trigger early retransmit).
  StreamFeedbackObserver::StreamPacketInfo lost_packet_feedback;
  lost_packet_feedback.rtp_sequence_number = rtp_sequence_numbers[0];
  lost_packet_feedback.ssrc = kSsrc1;
  lost_packet_feedback.received = false;
  lost_packet_feedback.is_retransmission = false;

  StreamFeedbackObserver::StreamPacketInfo received_packet_feedback;
  received_packet_feedback.rtp_sequence_number = rtp_sequence_numbers[1];
  received_packet_feedback.ssrc = kSsrc1;
  received_packet_feedback.received = true;
  lost_packet_feedback.is_retransmission = false;

  test.router()->OnPacketFeedbackVector(
      {lost_packet_feedback, received_packet_feedback});

  // Advance time to make sure retransmission would be allowed and try again.
  // This time the retransmission should not happen for the first packet since
  // the history has been notified of the ack and removed the packet. The
  // second packet, included in the feedback but not marked as received, should
  // still be retransmitted.
  test.AdvanceTime(TimeDelta::Millis(33));
  EXPECT_CALL(test.transport(), SendRtp)
      .WillOnce([&lost_packet_feedback](rtc::ArrayView<const uint8_t> packet,
                                        const PacketOptions& options) {
        RtpPacket rtp_packet;
        EXPECT_TRUE(rtp_packet.Parse(packet));
        EXPECT_EQ(rtp_packet.Ssrc(), kRtxSsrc1);
        // Capture the retransmitted sequence number from the RTX header.
        rtc::ArrayView<const uint8_t> payload = rtp_packet.payload();
        EXPECT_EQ(lost_packet_feedback.rtp_sequence_number,
                  ByteReader<uint16_t>::ReadBigEndian(payload.data()));
        return true;
      });
  test.router()->DeliverRtcp(nack_buffer.data(), nack_buffer.size());
  test.AdvanceTime(TimeDelta::Millis(33));
}

// This tests that we utilize transport wide feedback to retransmit lost
// packets. This is tested by dropping all ordinary packets from a "lossy"
// stream sent along with a secondary untouched stream. The transport wide
// feedback packets from the secondary stream allows the sending side to
// detect and retreansmit the lost packets from the lossy stream.
TEST(RtpVideoSenderTest, RetransmitsOnTransportWideLossInfo) {
  int rtx_packets;
  test::Scenario s(test_info_);
  test::CallClientConfig call_conf;
  // Keeping the bitrate fixed to avoid RTX due to probing.
  call_conf.transport.rates.max_rate = DataRate::KilobitsPerSec(300);
  call_conf.transport.rates.start_rate = DataRate::KilobitsPerSec(300);
  test::NetworkSimulationConfig net_conf;
  net_conf.bandwidth = DataRate::KilobitsPerSec(300);
  auto send_node = s.CreateSimulationNode(net_conf);
  auto* callee = s.CreateClient("return", call_conf);
  auto* route = s.CreateRoutes(s.CreateClient("send", call_conf), {send_node},
                               callee, {s.CreateSimulationNode(net_conf)});

  test::VideoStreamConfig lossy_config;
  lossy_config.source.framerate = 5;
  auto* lossy = s.CreateVideoStream(route->forward(), lossy_config);
  // The secondary stream acts a driver for transport feedback messages,
  // ensuring that lost packets on the lossy stream are retransmitted.
  s.CreateVideoStream(route->forward(), test::VideoStreamConfig());

  send_node->router()->SetFilter([&](const EmulatedIpPacket& packet) {
    RtpPacket rtp;
    if (rtp.Parse(packet.data)) {
      // Drops all regular packets for the lossy stream and counts all RTX
      // packets. Since no packets are let trough, NACKs can't be triggered
      // by the receiving side.
      if (lossy->send()->UsingSsrc(rtp.Ssrc())) {
        return false;
      } else if (lossy->send()->UsingRtxSsrc(rtp.Ssrc())) {
        ++rtx_packets;
      }
    }
    return true;
  });

  // Run for a short duration and reset counters to avoid counting RTX packets
  // from initial probing.
  s.RunFor(TimeDelta::Seconds(1));
  rtx_packets = 0;
  int decoded_baseline = 0;
  callee->SendTask([&decoded_baseline, &lossy]() {
    decoded_baseline = lossy->receive()->GetStats().frames_decoded;
  });
  s.RunFor(TimeDelta::Seconds(1));
  // We expect both that RTX packets were sent and that an appropriate number of
  // frames were received. This is somewhat redundant but reduces the risk of
  // false positives in future regressions (e.g. RTX is send due to probing).
  EXPECT_GE(rtx_packets, 1);
  int frames_decoded = 0;
  callee->SendTask([&decoded_baseline, &frames_decoded, &lossy]() {
    frames_decoded =
        lossy->receive()->GetStats().frames_decoded - decoded_baseline;
  });
  EXPECT_EQ(frames_decoded, 5);
}

// Integration test verifying that retransmissions are sent for packets which
// can be detected as lost early, using transport wide feedback.
TEST(RtpVideoSenderTest, EarlyRetransmits) {
  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {kRtxSsrc1, kRtxSsrc2},
                                 kPayloadType, {});
  test.SetSending(true);

  const uint8_t kPayload[1] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image._frameType = VideoFrameType::kVideoFrameKey;
  encoded_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));
  encoded_image.SetSimulcastIndex(0);

  CodecSpecificInfo codec_specific;
  codec_specific.codecType = VideoCodecType::kVideoCodecGeneric;

  // Send two tiny images, mapping to single RTP packets. Capture sequence
  // numbers.
  uint16_t frame1_rtp_sequence_number = 0;
  uint16_t frame1_transport_sequence_number = 0;
  EXPECT_CALL(test.transport(), SendRtp)
      .WillOnce(
          [&frame1_rtp_sequence_number, &frame1_transport_sequence_number](
              rtc::ArrayView<const uint8_t> packet,
              const PacketOptions& options) {
            RtpPacket rtp_packet;
            EXPECT_TRUE(rtp_packet.Parse(packet));
            frame1_rtp_sequence_number = rtp_packet.SequenceNumber();
            frame1_transport_sequence_number = options.packet_id;
            EXPECT_EQ(rtp_packet.Ssrc(), kSsrc1);
            return true;
          });
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);

  test.AdvanceTime(TimeDelta::Millis(33));

  uint16_t frame2_rtp_sequence_number = 0;
  uint16_t frame2_transport_sequence_number = 0;
  encoded_image.SetSimulcastIndex(1);
  EXPECT_CALL(test.transport(), SendRtp)
      .WillOnce(
          [&frame2_rtp_sequence_number, &frame2_transport_sequence_number](
              rtc::ArrayView<const uint8_t> packet,
              const PacketOptions& options) {
            RtpPacket rtp_packet;
            EXPECT_TRUE(rtp_packet.Parse(packet));
            frame2_rtp_sequence_number = rtp_packet.SequenceNumber();
            frame2_transport_sequence_number = options.packet_id;
            EXPECT_EQ(rtp_packet.Ssrc(), kSsrc2);
            return true;
          });
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);
  test.AdvanceTime(TimeDelta::Millis(33));

  EXPECT_NE(frame1_transport_sequence_number, frame2_transport_sequence_number);

  // Inject a transport feedback where the packet for the first frame is lost,
  // expect a retransmission for it.
  EXPECT_CALL(test.transport(), SendRtp)
      .WillOnce([&frame1_rtp_sequence_number](
                    rtc::ArrayView<const uint8_t> packet,
                    const PacketOptions& options) {
        RtpPacket rtp_packet;
        EXPECT_TRUE(rtp_packet.Parse(packet));
        EXPECT_EQ(rtp_packet.Ssrc(), kRtxSsrc1);

        // Retransmitted sequence number from the RTX header should match
        // the lost packet.
        rtc::ArrayView<const uint8_t> payload = rtp_packet.payload();
        EXPECT_EQ(ByteReader<uint16_t>::ReadBigEndian(payload.data()),
                  frame1_rtp_sequence_number);
        return true;
      });

  StreamFeedbackObserver::StreamPacketInfo first_packet_feedback;
  first_packet_feedback.rtp_sequence_number = frame1_rtp_sequence_number;
  first_packet_feedback.ssrc = kSsrc1;
  first_packet_feedback.received = false;
  first_packet_feedback.is_retransmission = false;

  StreamFeedbackObserver::StreamPacketInfo second_packet_feedback;
  second_packet_feedback.rtp_sequence_number = frame2_rtp_sequence_number;
  second_packet_feedback.ssrc = kSsrc2;
  second_packet_feedback.received = true;
  first_packet_feedback.is_retransmission = false;

  test.router()->OnPacketFeedbackVector(
      {first_packet_feedback, second_packet_feedback});

  // Wait for pacer to run and send the RTX packet.
  test.AdvanceTime(TimeDelta::Millis(33));
}

TEST(RtpVideoSenderTest, SupportsDependencyDescriptor) {
  RtpVideoSenderTestFixture test({kSsrc1}, {}, kPayloadType, {});
  test.SetSending(true);

  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> sent_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault([&](rtc::ArrayView<const uint8_t> packet,
                         const PacketOptions& options) {
        sent_packets.emplace_back(&extensions);
        EXPECT_TRUE(sent_packets.back().Parse(packet));
        return true;
      });

  const uint8_t kPayload[1] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));

  CodecSpecificInfo codec_specific;
  codec_specific.codecType = VideoCodecType::kVideoCodecGeneric;
  codec_specific.template_structure.emplace();
  codec_specific.template_structure->num_decode_targets = 1;
  codec_specific.template_structure->templates = {
      FrameDependencyTemplate().T(0).Dtis("S"),
      FrameDependencyTemplate().T(0).Dtis("S").FrameDiffs({2}),
      FrameDependencyTemplate().T(1).Dtis("D").FrameDiffs({1}),
  };

  // Send two tiny images, mapping to single RTP packets.
  // Send in key frame.
  encoded_image._frameType = VideoFrameType::kVideoFrameKey;
  codec_specific.generic_frame_info =
      GenericFrameInfo::Builder().T(0).Dtis("S").Build();
  codec_specific.generic_frame_info->encoder_buffers = {{0, false, true}};
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);
  test.AdvanceTime(TimeDelta::Millis(33));
  ASSERT_THAT(sent_packets, SizeIs(1));
  EXPECT_TRUE(
      sent_packets.back().HasExtension<RtpDependencyDescriptorExtension>());

  // Send in delta frame.
  encoded_image._frameType = VideoFrameType::kVideoFrameDelta;
  codec_specific.template_structure = absl::nullopt;
  codec_specific.generic_frame_info =
      GenericFrameInfo::Builder().T(1).Dtis("D").Build();
  codec_specific.generic_frame_info->encoder_buffers = {{0, true, false}};
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);
  test.AdvanceTime(TimeDelta::Millis(33));
  ASSERT_THAT(sent_packets, SizeIs(2));
  EXPECT_TRUE(
      sent_packets.back().HasExtension<RtpDependencyDescriptorExtension>());
}

TEST(RtpVideoSenderTest, SimulcastIndependentFrameIds) {
  test::ExplicitKeyValueConfig field_trials(
      "WebRTC-Video-SimulcastIndependentFrameIds/Enabled/"
      "WebRTC-GenericDescriptorAuth/Disabled/");
  const std::map<uint32_t, RtpPayloadState> kPayloadStates = {
      {kSsrc1, {.frame_id = 100}}, {kSsrc2, {.frame_id = 200}}};
  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {}, kPayloadType,
                                 kPayloadStates, &field_trials);
  test.SetSending(true);

  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> sent_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault([&](rtc::ArrayView<const uint8_t> packet,
                         const PacketOptions& options) {
        sent_packets.emplace_back(&extensions);
        EXPECT_TRUE(sent_packets.back().Parse(packet));
        return true;
      });

  const uint8_t kPayload[1] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));

  CodecSpecificInfo codec_specific;
  codec_specific.codecType = VideoCodecType::kVideoCodecGeneric;
  codec_specific.template_structure.emplace();
  codec_specific.template_structure->num_decode_targets = 1;
  codec_specific.template_structure->templates = {
      FrameDependencyTemplate().T(0).Dtis("S"),
      FrameDependencyTemplate().T(0).Dtis("S").FrameDiffs({1}),
  };
  codec_specific.generic_frame_info =
      GenericFrameInfo::Builder().T(0).Dtis("S").Build();
  encoded_image._frameType = VideoFrameType::kVideoFrameKey;
  codec_specific.generic_frame_info->encoder_buffers = {{0, false, true}};

  encoded_image.SetSimulcastIndex(0);
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);
  encoded_image.SetSimulcastIndex(1);
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);

  test.AdvanceTime(TimeDelta::Millis(33));
  ASSERT_THAT(sent_packets, SizeIs(2));
  DependencyDescriptorMandatory dd_s0;
  DependencyDescriptorMandatory dd_s1;
  ASSERT_TRUE(
      sent_packets[0].GetExtension<RtpDependencyDescriptorExtension>(&dd_s0));
  ASSERT_TRUE(
      sent_packets[1].GetExtension<RtpDependencyDescriptorExtension>(&dd_s1));
  EXPECT_EQ(dd_s0.frame_number(), 100);
  EXPECT_EQ(dd_s1.frame_number(), 200);
}

TEST(RtpVideoSenderTest,
     SimulcastNoIndependentFrameIdsIfGenericDescriptorAuthIsEnabled) {
  test::ExplicitKeyValueConfig field_trials(
      "WebRTC-Video-SimulcastIndependentFrameIds/Enabled/"
      "WebRTC-GenericDescriptorAuth/Enabled/");
  const std::map<uint32_t, RtpPayloadState> kPayloadStates = {
      {kSsrc1, {.shared_frame_id = 1000, .frame_id = 100}},
      {kSsrc2, {.shared_frame_id = 1000, .frame_id = 200}}};
  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {}, kPayloadType,
                                 kPayloadStates, &field_trials);
  test.SetSending(true);

  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> sent_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault([&](rtc::ArrayView<const uint8_t> packet,
                         const PacketOptions& options) {
        sent_packets.emplace_back(&extensions);
        EXPECT_TRUE(sent_packets.back().Parse(packet));
        return true;
      });

  const uint8_t kPayload[1] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));

  CodecSpecificInfo codec_specific;
  codec_specific.codecType = VideoCodecType::kVideoCodecGeneric;
  codec_specific.template_structure.emplace();
  codec_specific.template_structure->num_decode_targets = 1;
  codec_specific.template_structure->templates = {
      FrameDependencyTemplate().T(0).Dtis("S"),
      FrameDependencyTemplate().T(0).Dtis("S").FrameDiffs({1}),
  };
  codec_specific.generic_frame_info =
      GenericFrameInfo::Builder().T(0).Dtis("S").Build();
  encoded_image._frameType = VideoFrameType::kVideoFrameKey;
  codec_specific.generic_frame_info->encoder_buffers = {{0, false, true}};

  encoded_image.SetSimulcastIndex(0);
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);
  encoded_image.SetSimulcastIndex(1);
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);

  test.AdvanceTime(TimeDelta::Millis(33));
  ASSERT_THAT(sent_packets, SizeIs(2));
  DependencyDescriptorMandatory dd_s0;
  DependencyDescriptorMandatory dd_s1;
  ASSERT_TRUE(
      sent_packets[0].GetExtension<RtpDependencyDescriptorExtension>(&dd_s0));
  ASSERT_TRUE(
      sent_packets[1].GetExtension<RtpDependencyDescriptorExtension>(&dd_s1));
  EXPECT_EQ(dd_s0.frame_number(), 1001);
  EXPECT_EQ(dd_s1.frame_number(), 1002);
}

TEST(RtpVideoSenderTest,
     SupportsDependencyDescriptorForVp8NotProvidedByEncoder) {
  constexpr uint8_t kPayload[1] = {'a'};
  RtpVideoSenderTestFixture test({kSsrc1}, {}, kPayloadType, {});
  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> sent_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault(
          [&](rtc::ArrayView<const uint8_t> packet, const PacketOptions&) {
            EXPECT_TRUE(sent_packets.emplace_back(&extensions).Parse(packet));
            return true;
          });
  test.SetSending(true);

  EncodedImage key_frame_image;
  key_frame_image._frameType = VideoFrameType::kVideoFrameKey;
  key_frame_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));
  CodecSpecificInfo key_frame_info;
  key_frame_info.codecType = VideoCodecType::kVideoCodecVP8;
  ASSERT_EQ(
      test.router()->OnEncodedImage(key_frame_image, &key_frame_info).error,
      EncodedImageCallback::Result::OK);

  EncodedImage delta_image;
  delta_image._frameType = VideoFrameType::kVideoFrameDelta;
  delta_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));
  CodecSpecificInfo delta_info;
  delta_info.codecType = VideoCodecType::kVideoCodecVP8;
  ASSERT_EQ(test.router()->OnEncodedImage(delta_image, &delta_info).error,
            EncodedImageCallback::Result::OK);

  test.AdvanceTime(TimeDelta::Millis(123));

  DependencyDescriptor key_frame_dd;
  DependencyDescriptor delta_dd;
  ASSERT_THAT(sent_packets, SizeIs(2));
  EXPECT_TRUE(sent_packets[0].GetExtension<RtpDependencyDescriptorExtension>(
      /*structure=*/nullptr, &key_frame_dd));
  EXPECT_TRUE(sent_packets[1].GetExtension<RtpDependencyDescriptorExtension>(
      key_frame_dd.attached_structure.get(), &delta_dd));
}

TEST(RtpVideoSenderTest, SupportsDependencyDescriptorForVp9) {
  RtpVideoSenderTestFixture test({kSsrc1}, {}, kPayloadType, {});
  test.SetSending(true);

  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> sent_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault([&](rtc::ArrayView<const uint8_t> packet,
                         const PacketOptions& options) {
        sent_packets.emplace_back(&extensions);
        EXPECT_TRUE(sent_packets.back().Parse(packet));
        return true;
      });

  const uint8_t kPayload[1] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image._frameType = VideoFrameType::kVideoFrameKey;
  encoded_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));

  CodecSpecificInfo codec_specific;
  codec_specific.codecType = VideoCodecType::kVideoCodecVP9;
  codec_specific.template_structure.emplace();
  codec_specific.template_structure->num_decode_targets = 2;
  codec_specific.template_structure->templates = {
      FrameDependencyTemplate().S(0).Dtis("SS"),
      FrameDependencyTemplate().S(1).Dtis("-S").FrameDiffs({1}),
  };

  // Send two tiny images, each mapping to single RTP packet.
  // Send in key frame for the base spatial layer.
  codec_specific.generic_frame_info =
      GenericFrameInfo::Builder().S(0).Dtis("SS").Build();
  codec_specific.generic_frame_info->encoder_buffers = {{0, false, true}};
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);
  // Send in 2nd spatial layer.
  codec_specific.template_structure = absl::nullopt;
  codec_specific.generic_frame_info =
      GenericFrameInfo::Builder().S(1).Dtis("-S").Build();
  codec_specific.generic_frame_info->encoder_buffers = {{0, true, false},
                                                        {1, false, true}};
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);

  test.AdvanceTime(TimeDelta::Millis(33));
  ASSERT_THAT(sent_packets, SizeIs(2));
  EXPECT_TRUE(sent_packets[0].HasExtension<RtpDependencyDescriptorExtension>());
  EXPECT_TRUE(sent_packets[1].HasExtension<RtpDependencyDescriptorExtension>());
}

TEST(RtpVideoSenderTest,
     SupportsDependencyDescriptorForVp9NotProvidedByEncoder) {
  RtpVideoSenderTestFixture test({kSsrc1}, {}, kPayloadType, {});
  test.SetSending(true);

  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> sent_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault([&](rtc::ArrayView<const uint8_t> packet,
                         const PacketOptions& options) {
        sent_packets.emplace_back(&extensions);
        EXPECT_TRUE(sent_packets.back().Parse(packet));
        return true;
      });

  const uint8_t kPayload[1] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image._frameType = VideoFrameType::kVideoFrameKey;
  encoded_image._encodedWidth = 320;
  encoded_image._encodedHeight = 180;
  encoded_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));

  CodecSpecificInfo codec_specific;
  codec_specific.codecType = VideoCodecType::kVideoCodecVP9;
  codec_specific.codecSpecific.VP9.num_spatial_layers = 1;
  codec_specific.codecSpecific.VP9.temporal_idx = kNoTemporalIdx;
  codec_specific.codecSpecific.VP9.first_frame_in_picture = true;
  codec_specific.end_of_picture = true;
  codec_specific.codecSpecific.VP9.inter_pic_predicted = false;

  // Send two tiny images, each mapping to single RTP packet.
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);

  // Send in 2nd picture.
  encoded_image._frameType = VideoFrameType::kVideoFrameDelta;
  encoded_image.SetRtpTimestamp(3000);
  codec_specific.codecSpecific.VP9.inter_pic_predicted = true;
  codec_specific.codecSpecific.VP9.num_ref_pics = 1;
  codec_specific.codecSpecific.VP9.p_diff[0] = 1;
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);

  test.AdvanceTime(TimeDelta::Millis(33));
  ASSERT_THAT(sent_packets, SizeIs(2));
  EXPECT_TRUE(sent_packets[0].HasExtension<RtpDependencyDescriptorExtension>());
  EXPECT_TRUE(sent_packets[1].HasExtension<RtpDependencyDescriptorExtension>());
}

TEST(RtpVideoSenderTest, GenerateDependecyDescriptorForGenericCodecs) {
  test::ScopedKeyValueConfig field_trials(
      "WebRTC-GenericCodecDependencyDescriptor/Enabled/");
  RtpVideoSenderTestFixture test({kSsrc1}, {}, kPayloadType, {}, &field_trials);
  test.SetSending(true);

  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> sent_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault([&](rtc::ArrayView<const uint8_t> packet,
                         const PacketOptions& options) {
        sent_packets.emplace_back(&extensions);
        EXPECT_TRUE(sent_packets.back().Parse(packet));
        return true;
      });

  const uint8_t kPayload[1] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image._frameType = VideoFrameType::kVideoFrameKey;
  encoded_image._encodedWidth = 320;
  encoded_image._encodedHeight = 180;
  encoded_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));

  CodecSpecificInfo codec_specific;
  codec_specific.codecType = VideoCodecType::kVideoCodecGeneric;
  codec_specific.end_of_picture = true;

  // Send two tiny images, each mapping to single RTP packet.
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);

  // Send in 2nd picture.
  encoded_image._frameType = VideoFrameType::kVideoFrameDelta;
  encoded_image.SetRtpTimestamp(3000);
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);

  test.AdvanceTime(TimeDelta::Millis(33));
  ASSERT_THAT(sent_packets, SizeIs(2));
  EXPECT_TRUE(sent_packets[0].HasExtension<RtpDependencyDescriptorExtension>());
  EXPECT_TRUE(sent_packets[1].HasExtension<RtpDependencyDescriptorExtension>());
}

TEST(RtpVideoSenderTest, SupportsStoppingUsingDependencyDescriptor) {
  RtpVideoSenderTestFixture test({kSsrc1}, {}, kPayloadType, {});
  test.SetSending(true);

  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> sent_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault([&](rtc::ArrayView<const uint8_t> packet,
                         const PacketOptions& options) {
        sent_packets.emplace_back(&extensions);
        EXPECT_TRUE(sent_packets.back().Parse(packet));
        return true;
      });

  const uint8_t kPayload[1] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));

  CodecSpecificInfo codec_specific;
  codec_specific.codecType = VideoCodecType::kVideoCodecGeneric;
  codec_specific.template_structure.emplace();
  codec_specific.template_structure->num_decode_targets = 1;
  codec_specific.template_structure->templates = {
      FrameDependencyTemplate().T(0).Dtis("S"),
      FrameDependencyTemplate().T(0).Dtis("S").FrameDiffs({2}),
      FrameDependencyTemplate().T(1).Dtis("D").FrameDiffs({1}),
  };

  // Send two tiny images, mapping to single RTP packets.
  // Send in a key frame.
  encoded_image._frameType = VideoFrameType::kVideoFrameKey;
  codec_specific.generic_frame_info =
      GenericFrameInfo::Builder().T(0).Dtis("S").Build();
  codec_specific.generic_frame_info->encoder_buffers = {{0, false, true}};
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);
  test.AdvanceTime(TimeDelta::Millis(33));
  ASSERT_THAT(sent_packets, SizeIs(1));
  EXPECT_TRUE(
      sent_packets.back().HasExtension<RtpDependencyDescriptorExtension>());

  // Send in a new key frame without the support for the dependency descriptor.
  encoded_image._frameType = VideoFrameType::kVideoFrameKey;
  codec_specific.template_structure = absl::nullopt;
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);
  test.AdvanceTime(TimeDelta::Millis(33));
  ASSERT_THAT(sent_packets, SizeIs(2));
  EXPECT_FALSE(
      sent_packets.back().HasExtension<RtpDependencyDescriptorExtension>());
}

TEST(RtpVideoSenderTest, CanSetZeroBitrate) {
  RtpVideoSenderTestFixture test({kSsrc1}, {kRtxSsrc1}, kPayloadType, {});
  test.router()->OnBitrateUpdated(CreateBitrateAllocationUpdate(0),
                                  /*framerate*/ 0);
}

TEST(RtpVideoSenderTest, SimulcastSenderRegistersFrameTransformers) {
  rtc::scoped_refptr<MockFrameTransformer> transformer =
      rtc::make_ref_counted<MockFrameTransformer>();

  EXPECT_CALL(*transformer, RegisterTransformedFrameSinkCallback(_, kSsrc1));
  EXPECT_CALL(*transformer, RegisterTransformedFrameSinkCallback(_, kSsrc2));
  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {kRtxSsrc1, kRtxSsrc2},
                                 kPayloadType, {}, nullptr, transformer);

  EXPECT_CALL(*transformer, UnregisterTransformedFrameSinkCallback(kSsrc1));
  EXPECT_CALL(*transformer, UnregisterTransformedFrameSinkCallback(kSsrc2));
}

TEST(RtpVideoSenderTest, OverheadIsSubtractedFromTargetBitrate) {
  test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-UseFrameRateForOverhead/Enabled/");

  // TODO(jakobi): RTP header size should not be hard coded.
  constexpr uint32_t kRtpHeaderSizeBytes = 20;
  constexpr uint32_t kTransportPacketOverheadBytes = 40;
  constexpr uint32_t kOverheadPerPacketBytes =
      kRtpHeaderSizeBytes + kTransportPacketOverheadBytes;
  RtpVideoSenderTestFixture test({kSsrc1}, {}, kPayloadType, {}, &field_trials);
  test.router()->OnTransportOverheadChanged(kTransportPacketOverheadBytes);
  test.SetSending(true);

  {
    test.router()->OnBitrateUpdated(CreateBitrateAllocationUpdate(300000),
                                    /*framerate*/ 15);
    // 1 packet per frame.
    EXPECT_EQ(test.router()->GetPayloadBitrateBps(),
              300000 - kOverheadPerPacketBytes * 8 * 30);
  }
  {
    test.router()->OnBitrateUpdated(CreateBitrateAllocationUpdate(150000),
                                    /*framerate*/ 15);
    // 1 packet per frame.
    EXPECT_EQ(test.router()->GetPayloadBitrateBps(),
              150000 - kOverheadPerPacketBytes * 8 * 15);
  }
  {
    test.router()->OnBitrateUpdated(CreateBitrateAllocationUpdate(1000000),
                                    /*framerate*/ 30);
    // 3 packets per frame.
    EXPECT_EQ(test.router()->GetPayloadBitrateBps(),
              1000000 - kOverheadPerPacketBytes * 8 * 30 * 3);
  }
}

TEST(RtpVideoSenderTest, ClearsPendingPacketsOnInactivation) {
  RtpVideoSenderTestFixture test({kSsrc1}, {kRtxSsrc1}, kPayloadType, {});
  test.SetSending(true);

  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> sent_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault([&](rtc::ArrayView<const uint8_t> packet,
                         const PacketOptions& options) {
        sent_packets.emplace_back(&extensions);
        EXPECT_TRUE(sent_packets.back().Parse(packet));
        return true;
      });

  // Set a very low bitrate.
  test.router()->OnBitrateUpdated(
      CreateBitrateAllocationUpdate(/*rate_bps=*/10'000),
      /*framerate=*/30);

  // Create and send a large keyframe.
  const size_t kImageSizeBytes = 10000;
  constexpr uint8_t kPayload[kImageSizeBytes] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image._frameType = VideoFrameType::kVideoFrameKey;
  encoded_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));
  EXPECT_EQ(test.router()
                ->OnEncodedImage(encoded_image, /*codec_specific=*/nullptr)
                .error,
            EncodedImageCallback::Result::OK);

  // Advance time a small amount, check that sent data is only part of the
  // image.
  test.AdvanceTime(TimeDelta::Millis(5));
  DataSize transmittedPayload = DataSize::Zero();
  for (const RtpPacket& packet : sent_packets) {
    transmittedPayload += DataSize::Bytes(packet.payload_size());
    // Make sure we don't see the end of the frame.
    EXPECT_FALSE(packet.Marker());
  }
  EXPECT_GT(transmittedPayload, DataSize::Zero());
  EXPECT_LT(transmittedPayload, DataSize::Bytes(kImageSizeBytes / 3));

  // Record the RTP timestamp of the first frame.
  const uint32_t first_frame_timestamp = sent_packets[0].Timestamp();
  sent_packets.clear();

  // Disable the sending module and advance time slightly. No packets should be
  // sent.
  test.SetSending(false);
  test.AdvanceTime(TimeDelta::Millis(20));
  EXPECT_TRUE(sent_packets.empty());

  // Reactive the send module - any packets should have been removed, so nothing
  // should be transmitted.
  test.SetSending(true);
  test.AdvanceTime(TimeDelta::Millis(33));
  EXPECT_TRUE(sent_packets.empty());

  // Send a new frame.
  encoded_image.SetRtpTimestamp(3);
  encoded_image.capture_time_ms_ = 4;
  EXPECT_EQ(test.router()
                ->OnEncodedImage(encoded_image, /*codec_specific=*/nullptr)
                .error,
            EncodedImageCallback::Result::OK);
  test.AdvanceTime(TimeDelta::Millis(33));

  // Advance time, check we get new packets - but only for the second frame.
  EXPECT_FALSE(sent_packets.empty());
  EXPECT_NE(sent_packets[0].Timestamp(), first_frame_timestamp);
}

// Integration test verifying that when retransmission mode is set to
// kRetransmitBaseLayer,only base layer is retransmitted.
TEST(RtpVideoSenderTest, RetransmitsBaseLayerOnly) {
  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {kRtxSsrc1, kRtxSsrc2},
                                 kPayloadType, {});
  test.SetSending(true);

  test.router()->SetRetransmissionMode(kRetransmitBaseLayer);
  constexpr uint8_t kPayload = 'a';
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image._frameType = VideoFrameType::kVideoFrameKey;
  encoded_image.SetEncodedData(EncodedImageBuffer::Create(&kPayload, 1));

  // Send two tiny images, mapping to two RTP packets. Capture sequence numbers.
  std::vector<uint16_t> rtp_sequence_numbers;
  std::vector<uint16_t> transport_sequence_numbers;
  std::vector<uint16_t> base_sequence_numbers;
  EXPECT_CALL(test.transport(), SendRtp)
      .Times(2)
      .WillRepeatedly([&rtp_sequence_numbers, &transport_sequence_numbers](
                          rtc::ArrayView<const uint8_t> packet,
                          const PacketOptions& options) {
        RtpPacket rtp_packet;
        EXPECT_TRUE(rtp_packet.Parse(packet));
        rtp_sequence_numbers.push_back(rtp_packet.SequenceNumber());
        transport_sequence_numbers.push_back(options.packet_id);
        return true;
      });
  CodecSpecificInfo key_codec_info;
  key_codec_info.codecType = kVideoCodecVP8;
  key_codec_info.codecSpecific.VP8.temporalIdx = 0;
  EXPECT_EQ(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(
                encoded_image, &key_codec_info).error);
  encoded_image.SetRtpTimestamp(2);
  encoded_image.capture_time_ms_ = 3;
  encoded_image._frameType = VideoFrameType::kVideoFrameDelta;
  CodecSpecificInfo delta_codec_info;
  delta_codec_info.codecType = kVideoCodecVP8;
  delta_codec_info.codecSpecific.VP8.temporalIdx = 1;
  EXPECT_EQ(EncodedImageCallback::Result::OK,
            test.router()->OnEncodedImage(
                encoded_image, &delta_codec_info).error);

  test.AdvanceTime(TimeDelta::Millis(33));

  // Construct a NACK message for requesting retransmission of both packet.
  rtcp::Nack nack;
  nack.SetMediaSsrc(kSsrc1);
  nack.SetPacketIds(rtp_sequence_numbers);
  rtc::Buffer nack_buffer = nack.Build();

  std::vector<uint16_t> retransmitted_rtp_sequence_numbers;
  EXPECT_CALL(test.transport(), SendRtp)
      .Times(1)
      .WillRepeatedly([&retransmitted_rtp_sequence_numbers](
                          rtc::ArrayView<const uint8_t> packet,
                          const PacketOptions& options) {
        RtpPacket rtp_packet;
        EXPECT_TRUE(rtp_packet.Parse(packet));
        EXPECT_EQ(rtp_packet.Ssrc(), kRtxSsrc1);
        // Capture the retransmitted sequence number from the RTX header.
        rtc::ArrayView<const uint8_t> payload = rtp_packet.payload();
        retransmitted_rtp_sequence_numbers.push_back(
            ByteReader<uint16_t>::ReadBigEndian(payload.data()));
        return true;
      });
  test.router()->DeliverRtcp(nack_buffer.data(), nack_buffer.size());
  test.AdvanceTime(TimeDelta::Millis(33));

  // Verify that only base layer packet was retransmitted.
  std::vector<uint16_t> base_rtp_sequence_numbers(rtp_sequence_numbers.begin(),
                               rtp_sequence_numbers.begin() + 1);
  EXPECT_EQ(retransmitted_rtp_sequence_numbers, base_rtp_sequence_numbers);
}

}  // namespace webrtc
