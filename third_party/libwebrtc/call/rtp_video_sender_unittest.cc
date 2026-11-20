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

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/call/bitrate_allocation.h"
#include "api/call/transport.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/fec_controller.h"
#include "api/frame_transformer_interface.h"
#include "api/make_ref_counted.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_headers.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/test/mock_frame_transformer.h"
#include "api/test/network_emulation/network_emulation_interfaces.h"
#include "api/transport/bitrate_settings.h"
#include "api/transport/rtp/dependency_descriptor.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/encoded_image.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_layers_allocation.h"
#include "api/video_codecs/video_encoder.h"
#include "call/rtp_config.h"
#include "call/rtp_transport_config.h"
#include "call/rtp_transport_controller_send.h"
#include "call/rtp_transport_controller_send_interface.h"
#include "call/video_send_stream.h"
#include "common_video/frame_counts.h"
#include "common_video/generic_frame_descriptor/generic_frame_info.h"
#include "modules/rtp_rtcp/include/report_block_data.h"
#include "modules/rtp_rtcp/include/rtcp_statistics.h"
#include "modules/rtp_rtcp/include/rtp_header_extension_map.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "modules/rtp_rtcp/source/rtcp_packet/nack.h"
#include "modules/rtp_rtcp/source/rtp_dependency_descriptor_extension.h"
#include "modules/rtp_rtcp/source/rtp_packet.h"
#include "modules/video_coding/codecs/interface/common_constants.h"
#include "modules/video_coding/fec_controller_default.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "rtc_base/buffer.h"
#include "rtc_base/checks.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/mock_transport.h"
#include "test/scenario/scenario.h"
#include "test/scenario/scenario_config.h"
#include "test/time_controller/simulated_time_controller.h"
#include "video/config/video_encoder_config.h"
#include "video/send_statistics_proxy.h"

namespace webrtc {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Ge;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SizeIs;

constexpr int8_t kPayloadType = 96;
constexpr int8_t kPayloadType2 = 98;
constexpr uint32_t kSsrc1 = 12345;
constexpr uint32_t kSsrc2 = 23456;
constexpr uint32_t kRtxSsrc1 = 34567;
constexpr uint32_t kRtxSsrc2 = 45678;
constexpr int16_t kInitialPictureId1 = 222;
constexpr int16_t kInitialPictureId2 = 44;
constexpr int16_t kInitialTl0PicIdx1 = 99;
constexpr int16_t kInitialTl0PicIdx2 = 199;
constexpr RtpHeaderExtensionId kTransportsSequenceExtensionId(7);
constexpr RtpHeaderExtensionId kDependencyDescriptorExtensionId(8);

class MockRtcpIntraFrameObserver : public RtcpIntraFrameObserver {
 public:
  MOCK_METHOD(void, OnReceivedIntraFrameRequest, (uint32_t), (override));
};

class MockFecController : public FecController {
 public:
  MOCK_METHOD(void,
              SetProtectionCallback,
              (VCMProtectionCallback * protection_callback),
              (override));
  MOCK_METHOD(void,
              SetProtectionMethod,
              (bool enable_fec, bool enable_nack),
              (override));
  MOCK_METHOD(void,
              SetEncodingData,
              (size_t width,
               size_t height,
               size_t num_temporal_layers,
               size_t max_payload_size),
              (override));
  MOCK_METHOD(uint32_t,
              UpdateFecRates,
              (uint32_t estimated_bitrate_bps,
               int actual_framerate,
               uint8_t fraction_lost,
               std::vector<bool> loss_mask_vector,
               int64_t round_trip_time_ms),
              (override));
  MOCK_METHOD(void,
              UpdateWithEncodedData,
              (size_t encoded_image_length,
               VideoFrameType encoded_image_frametype),
              (override));
  MOCK_METHOD(bool, UseLossVectorMask, (), (override));
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
    int payload_type,
    std::span<const int> payload_types,
    const std::vector<uint32_t>& csrcs) {
  VideoSendStream::Config config(transport);
  config.rtp.ssrcs = ssrcs;
  config.rtp.rtx.ssrcs = rtx_ssrcs;
  config.rtp.csrcs = csrcs;
  config.rtp.payload_type = payload_type;
  config.rtp.rtx.payload_type = payload_type + 1;
  config.rtp.nack.rtp_history_ms = 1000;
  config.rtp.extensions.emplace_back(RtpExtension::kTransportSequenceNumberUri,
                                     kTransportsSequenceExtensionId);
  config.rtp.extensions.emplace_back(RtpDependencyDescriptorExtension::Uri(),
                                     kDependencyDescriptorExtensionId);
  config.rtp.extmap_allow_mixed = true;

  if (!payload_types.empty()) {
    RTC_CHECK_EQ(payload_types.size(), ssrcs.size());
    for (size_t i = 0; i < ssrcs.size(); ++i) {
      auto& stream_config = config.rtp.stream_configs.emplace_back();
      stream_config.ssrc = ssrcs[i];
      stream_config.payload_type = payload_types[i];
      if (i < rtx_ssrcs.size()) {
        auto& rtx = stream_config.rtx.emplace();
        rtx.ssrc = rtx_ssrcs[i];
        rtx.payload_type = payload_types[i] + 1;
      }
    }
  }
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
      scoped_refptr<FrameTransformerInterface> frame_transformer,
      const std::vector<int>& payload_types,
      absl::string_view field_trials = "",
      const std::vector<uint32_t>& csrcs = {},
      std::unique_ptr<FecController> fec_controller = nullptr)
      : time_controller_(Timestamp::Millis(1000000)),
        env_(CreateTestEnvironment(
            {.field_trials = field_trials, .time = &time_controller_})),
        config_(CreateVideoSendStreamConfig(&transport_,
                                            ssrcs,
                                            rtx_ssrcs,
                                            payload_type,
                                            payload_types,
                                            csrcs)),
        bitrate_config_(GetBitrateConfig()),
        transport_controller_(RtpTransportConfig{
            .env = env_,
            .bitrate_config = bitrate_config_,
            .worker_thread = time_controller_.GetMainThread()}),
        stats_proxy_(env_,
                     config_,
                     VideoEncoderConfig::ContentType::kRealtimeVideo) {
    transport_controller_.EnsureStarted();
    std::map<uint32_t, RtpState> suspended_ssrcs;
    if (!fec_controller) {
      fec_controller = std::make_unique<FecControllerDefault>(env_);
    }
    router_ = std::make_unique<RtpVideoSender>(
        env_, time_controller_.GetMainThread(), suspended_ssrcs,
        suspended_payload_states, config_.rtp, config_.rtcp_report_interval_ms,
        &transport_,
        CreateObservers(&encoder_feedback_, &stats_proxy_, &stats_proxy_,
                        &stats_proxy_, frame_count_observer, &stats_proxy_),
        &transport_controller_, std::move(fec_controller), nullptr,
        CryptoOptions{}, frame_transformer);
  }
  RtpVideoSenderTestFixture(
      const std::vector<uint32_t>& ssrcs,
      const std::vector<uint32_t>& rtx_ssrcs,
      int payload_type,
      const std::map<uint32_t, RtpPayloadState>& suspended_payload_states,
      FrameCountObserver* frame_count_observer,
      scoped_refptr<FrameTransformerInterface> frame_transformer,
      absl::string_view field_trials = "")
      : RtpVideoSenderTestFixture(ssrcs,
                                  rtx_ssrcs,
                                  payload_type,
                                  suspended_payload_states,
                                  frame_count_observer,
                                  frame_transformer,
                                  /*payload_types=*/{},
                                  field_trials) {}

  RtpVideoSenderTestFixture(
      const std::vector<uint32_t>& ssrcs,
      const std::vector<uint32_t>& rtx_ssrcs,
      int payload_type,
      const std::map<uint32_t, RtpPayloadState>& suspended_payload_states,
      FrameCountObserver* frame_count_observer,
      absl::string_view field_trials = "")
      : RtpVideoSenderTestFixture(ssrcs,
                                  rtx_ssrcs,
                                  payload_type,
                                  suspended_payload_states,
                                  frame_count_observer,
                                  /*frame_transformer=*/nullptr,
                                  /*payload_types=*/{},
                                  field_trials) {}

  RtpVideoSenderTestFixture(
      const std::vector<uint32_t>& ssrcs,
      const std::vector<uint32_t>& rtx_ssrcs,
      int payload_type,
      const std::map<uint32_t, RtpPayloadState>& suspended_payload_states,
      absl::string_view field_trials = "")
      : RtpVideoSenderTestFixture(ssrcs,
                                  rtx_ssrcs,
                                  payload_type,
                                  suspended_payload_states,
                                  /*frame_count_observer=*/nullptr,
                                  /*frame_transformer=*/nullptr,
                                  /*payload_types=*/{},
                                  field_trials) {}

  ~RtpVideoSenderTestFixture() { SetSending(false); }

  RtpVideoSender* router() { return router_.get(); }
  MockTransport& transport() { return transport_; }
  void AdvanceTime(TimeDelta delta) { time_controller_.AdvanceTime(delta); }

  void SetSending(bool sending) { router_->SetSending(sending); }

 private:
  NiceMock<MockTransport> transport_;
  NiceMock<MockRtcpIntraFrameObserver> encoder_feedback_;
  GlobalSimulatedTimeController time_controller_;
  Environment env_;
  VideoSendStream::Config config_;
  BitrateConstraints bitrate_config_;
  RtpTransportControllerSend transport_controller_;
  SendStatisticsProxy stats_proxy_;
  std::unique_ptr<RtpVideoSender> router_;
};

BitrateAllocationUpdate CreateBitrateAllocationUpdate(
    int target_bitrate_bps,
    DataSize packet_overhead = DataSize::Zero()) {
  BitrateAllocationUpdate update;
  update.target_bitrate = DataRate::BitsPerSec(target_bitrate_bps);
  update.round_trip_time = TimeDelta::Zero();
  update.packet_overhead = packet_overhead;
  return update;
}

}  // namespace

TEST(RtpVideoSenderTest, SendOnOneModule) {
  constexpr uint8_t kPayload = 'a';
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
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
  encoded_image_1.set_frame_type(VideoFrameType::kVideoFrameKey);
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
  encoded_image_1.set_frame_type(VideoFrameType::kVideoFrameKey);
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
  encoded_image_1.set_frame_type(VideoFrameType::kVideoFrameKey);
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
  test.AdvanceTime(TimeDelta::Millis(33));
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
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
  encoded_image.SetEncodedData(EncodedImageBuffer::Create(&kPayload, 1));

  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);

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

  encoded_image.set_frame_type(VideoFrameType::kVideoFrameDelta);
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
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
  encoded_image.SetEncodedData(EncodedImageBuffer::Create(&kPayload, 1));

  // Send two tiny images, mapping to two RTP packets. Capture sequence numbers.
  std::vector<uint16_t> rtp_sequence_numbers;
  std::vector<uint16_t> transport_sequence_numbers;
  EXPECT_CALL(test.transport(), SendRtp)
      .Times(2)
      .WillRepeatedly(
          [&rtp_sequence_numbers, &transport_sequence_numbers](
              std::span<const uint8_t> packet, const PacketOptions& options) {
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
  Buffer nack_buffer = nack.Build();

  std::vector<uint16_t> retransmitted_rtp_sequence_numbers;
  EXPECT_CALL(test.transport(), SendRtp)
      .Times(2)
      .WillRepeatedly([&retransmitted_rtp_sequence_numbers](
                          std::span<const uint8_t> packet,
                          const PacketOptions& options) {
        RtpPacket rtp_packet;
        EXPECT_TRUE(rtp_packet.Parse(packet));
        EXPECT_EQ(rtp_packet.Ssrc(), kRtxSsrc1);
        // Capture the retransmitted sequence number from the RTX header.
        std::span<const uint8_t> payload = rtp_packet.payload();
        retransmitted_rtp_sequence_numbers.push_back(
            ByteReader<uint16_t>::ReadBigEndian(payload.data()));
        return true;
      });
  test.router()->DeliverRtcp(nack_buffer);
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
      .WillOnce([&lost_packet_feedback](std::span<const uint8_t> packet,
                                        const PacketOptions& options) {
        RtpPacket rtp_packet;
        EXPECT_TRUE(rtp_packet.Parse(packet));
        EXPECT_EQ(rtp_packet.Ssrc(), kRtxSsrc1);
        // Capture the retransmitted sequence number from the RTX header.
        std::span<const uint8_t> payload = rtp_packet.payload();
        EXPECT_EQ(lost_packet_feedback.rtp_sequence_number,
                  ByteReader<uint16_t>::ReadBigEndian(payload.data()));
        return true;
      });
  test.router()->DeliverRtcp(nack_buffer);
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
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
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
              std::span<const uint8_t> packet, const PacketOptions& options) {
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
              std::span<const uint8_t> packet, const PacketOptions& options) {
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
      .WillOnce([&frame1_rtp_sequence_number](std::span<const uint8_t> packet,
                                              const PacketOptions& options) {
        RtpPacket rtp_packet;
        EXPECT_TRUE(rtp_packet.Parse(packet));
        EXPECT_EQ(rtp_packet.Ssrc(), kRtxSsrc1);

        // Retransmitted sequence number from the RTX header should match
        // the lost packet.
        std::span<const uint8_t> payload = rtp_packet.payload();
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

TEST(RtpVideoSenderTest, SendsCsrcsFromConfig) {
  // One more CSRC than fits in an RTP header.
  std::vector<uint32_t> csrcs(kRtpCsrcSize + 1);
  for (size_t i = 0; i < csrcs.size(); ++i) {
    csrcs[i] = 1000 + i;
  }
  RtpVideoSenderTestFixture test({kSsrc1}, {}, kPayloadType, {},
                                 /*frame_count_observer=*/nullptr,
                                 /*frame_transformer=*/nullptr,
                                 /*payload_types=*/{}, /*field_trials=*/"",
                                 csrcs);
  test.SetSending(true);

  std::vector<RtpPacket> sent_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault(
          [&](std::span<const uint8_t> packet, const PacketOptions& options) {
            sent_packets.emplace_back();
            EXPECT_TRUE(sent_packets.back().Parse(packet));
            return true;
          });

  const uint8_t kPayload[1] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
  encoded_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, nullptr).error,
            EncodedImageCallback::Result::OK);
  test.AdvanceTime(TimeDelta::Millis(33));

  ASSERT_THAT(sent_packets, SizeIs(1));
  EXPECT_THAT(sent_packets[0].Csrcs(),
              ElementsAreArray(csrcs.begin(), csrcs.begin() + kRtpCsrcSize));
}

TEST(RtpVideoSenderTest, SupportsDependencyDescriptor) {
  RtpVideoSenderTestFixture test({kSsrc1}, {}, kPayloadType, {});
  test.SetSending(true);

  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> sent_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault(
          [&](std::span<const uint8_t> packet, const PacketOptions& options) {
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
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
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
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameDelta);
  codec_specific.template_structure = std::nullopt;
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

// Frames of a disabled simulcast layer, e.g. frames in flight in the encoder
// when the layer was disabled, are dropped. If a later frame of the layer
// references such a frame, the dependency descriptor must say so rather than
// reference the last frame the layer sent. Since simulcast layers share frame
// ids, that frame can be arbitrarily far back.
TEST(RtpVideoSenderTest,
     DescribesDependencyOnFrameDroppedWhileLayerWasDisabled) {
  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {}, kPayloadType, {});
  test.SetSending(true);

  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> layer1_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault(
          [&](std::span<const uint8_t> packet, const PacketOptions& options) {
            RtpPacket rtp_packet(&extensions);
            EXPECT_TRUE(rtp_packet.Parse(packet));
            if (rtp_packet.Ssrc() == kSsrc2 && rtp_packet.payload_size() > 0) {
              layer1_packets.push_back(rtp_packet);
            }
            return true;
          });

  const uint8_t kPayload[1] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));

  CodecSpecificInfo key_frame_info;
  key_frame_info.codecType = VideoCodecType::kVideoCodecGeneric;
  key_frame_info.template_structure.emplace();
  key_frame_info.template_structure->num_decode_targets = 1;
  key_frame_info.template_structure->templates = {
      FrameDependencyTemplate().T(0).Dtis("S"),
      FrameDependencyTemplate().T(0).Dtis("S").FrameDiffs({1}),
  };
  key_frame_info.generic_frame_info =
      GenericFrameInfo::Builder().T(0).Dtis("S").Build();
  key_frame_info.generic_frame_info->encoder_buffers = {
      {/*id=*/0, /*referenced=*/false, /*updated=*/true}};
  CodecSpecificInfo delta_frame_info = key_frame_info;
  delta_frame_info.template_structure = std::nullopt;
  delta_frame_info.generic_frame_info->encoder_buffers = {
      {/*id=*/0, /*referenced=*/true, /*updated=*/true}};

  auto send_frame = [&](int simulcast_index, VideoFrameType frame_type) {
    encoded_image.SetSimulcastIndex(simulcast_index);
    encoded_image.set_frame_type(frame_type);
    const CodecSpecificInfo& info = frame_type == VideoFrameType::kVideoFrameKey
                                        ? key_frame_info
                                        : delta_frame_info;
    return test.router()->OnEncodedImage(encoded_image, &info).error;
  };

  EXPECT_EQ(send_frame(0, VideoFrameType::kVideoFrameKey),
            EncodedImageCallback::Result::OK);
  EXPECT_EQ(send_frame(1, VideoFrameType::kVideoFrameKey),
            EncodedImageCallback::Result::OK);
  test.AdvanceTime(TimeDelta::Millis(33));
  ASSERT_THAT(layer1_packets, SizeIs(1));
  DependencyDescriptor key_frame_dd;
  ASSERT_TRUE(layer1_packets[0].GetExtension<RtpDependencyDescriptorExtension>(
      /*structure=*/nullptr, &key_frame_dd));
  ASSERT_THAT(key_frame_dd.attached_structure, NotNull());

  // Disable layer 1. Frames on layer 0 keep advancing the shared frame id.
  test.router()->OnVideoLayersAllocationUpdated(
      {.active_spatial_layers = {{.rtp_stream_index = 0}}});
  test.AdvanceTime(TimeDelta::Zero());
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(send_frame(0, VideoFrameType::kVideoFrameDelta),
              EncodedImageCallback::Result::OK);
    test.AdvanceTime(TimeDelta::Millis(33));
  }

  // A frame of the disabled layer is dropped.
  EXPECT_NE(send_frame(1, VideoFrameType::kVideoFrameKey),
            EncodedImageCallback::Result::OK);

  // Re-enable layer 1.
  test.router()->OnVideoLayersAllocationUpdated(
      {.active_spatial_layers = {{.rtp_stream_index = 0},
                                 {.rtp_stream_index = 1}}});
  test.AdvanceTime(TimeDelta::Zero());

  // The next frame on layer 1 references the dropped key frame.
  EXPECT_EQ(send_frame(1, VideoFrameType::kVideoFrameDelta),
            EncodedImageCallback::Result::OK);
  test.AdvanceTime(TimeDelta::Millis(33));
  ASSERT_THAT(layer1_packets, SizeIs(2));
  DependencyDescriptor delta_frame_dd;
  ASSERT_TRUE(layer1_packets[1].GetExtension<RtpDependencyDescriptorExtension>(
      key_frame_dd.attached_structure.get(), &delta_frame_dd));
  EXPECT_THAT(delta_frame_dd.frame_dependencies.frame_diffs, ElementsAre(1));
}

// The encoder produces a key frame for a re-enabled layer right away, possibly
// before the layer's RTP module has been activated on the transport queue. The
// key frame must not be dropped.
TEST(RtpVideoSenderTest, SendsKeyFrameOfReEnabledLayerBeforeModuleIsActivated) {
  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {kRtxSsrc1, kRtxSsrc2},
                                 kPayloadType, {});
  test.SetSending(true);

  std::vector<RtpPacket> layer1_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault(
          [&](std::span<const uint8_t> packet, const PacketOptions& options) {
            RtpPacket rtp_packet;
            EXPECT_TRUE(rtp_packet.Parse(packet));
            if (rtp_packet.Ssrc() == kSsrc2 && rtp_packet.payload_size() > 0) {
              layer1_packets.push_back(rtp_packet);
            }
            return true;
          });

  constexpr uint8_t kPayload = 'a';
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
  encoded_image.SetEncodedData(EncodedImageBuffer::Create(&kPayload, 1));
  encoded_image.SetSimulcastIndex(1);
  CodecSpecificInfo codec_info;
  codec_info.codecType = kVideoCodecVP8;

  // Disable layer 1.
  test.router()->OnVideoLayersAllocationUpdated(
      {.active_spatial_layers = {{.rtp_stream_index = 0}}});
  test.AdvanceTime(TimeDelta::Zero());

  // Re-enable layer 1 and deliver its key frame before the task that activates
  // the RTP module has run.
  test.router()->OnVideoLayersAllocationUpdated(
      {.active_spatial_layers = {{.rtp_stream_index = 0},
                                 {.rtp_stream_index = 1}}});
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_info).error,
            EncodedImageCallback::Result::OK);
  test.AdvanceTime(TimeDelta::Millis(33));
  EXPECT_THAT(layer1_packets, SizeIs(1));
}

TEST(RtpVideoSenderTest, SimulcastIndependentFrameIds) {
  absl::string_view field_trials = "WebRTC-GenericDescriptorAuth/Disabled/";
  const std::map<uint32_t, RtpPayloadState> kPayloadStates = {
      {kSsrc1, {.frame_id = 100}}, {kSsrc2, {.frame_id = 200}}};
  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {}, kPayloadType,
                                 kPayloadStates, field_trials);
  test.SetSending(true);

  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> sent_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault(
          [&](std::span<const uint8_t> packet, const PacketOptions& options) {
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
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
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
  absl::string_view field_trials = "WebRTC-GenericDescriptorAuth/Enabled/";
  const std::map<uint32_t, RtpPayloadState> kPayloadStates = {
      {kSsrc1, {.shared_frame_id = 1000, .frame_id = 100}},
      {kSsrc2, {.shared_frame_id = 1000, .frame_id = 200}}};
  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {}, kPayloadType,
                                 kPayloadStates, field_trials);
  test.SetSending(true);

  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> sent_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault(
          [&](std::span<const uint8_t> packet, const PacketOptions& options) {
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
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
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

TEST(RtpVideoSenderTest, MixedCodecSimulcastPayloadType) {
  // When multiple payload types are set, verify that the payload type switches
  // corresponding to the simulcast index.
  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {kRtxSsrc1, kRtxSsrc2},
                                 kPayloadType, {}, nullptr, nullptr,
                                 {kPayloadType, kPayloadType2});
  test.SetSending(true);

  std::vector<uint16_t> rtp_sequence_numbers;
  std::vector<RtpPacket> sent_packets;
  EXPECT_CALL(test.transport(), SendRtp)
      .Times(3)
      .WillRepeatedly([&](std::span<const uint8_t> packet,
                          const PacketOptions& options) -> bool {
        RtpPacket& rtp_packet = sent_packets.emplace_back();
        EXPECT_TRUE(rtp_packet.Parse(packet));
        rtp_sequence_numbers.push_back(rtp_packet.SequenceNumber());
        return true;
      });

  const uint8_t kPayload[1] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));

  CodecSpecificInfo codec_specific;
  codec_specific.codecType = VideoCodecType::kVideoCodecVP8;

  encoded_image.SetSimulcastIndex(0);
  ASSERT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);
  ASSERT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);
  encoded_image.SetSimulcastIndex(1);
  ASSERT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);

  test.AdvanceTime(TimeDelta::Millis(33));
  ASSERT_THAT(sent_packets, SizeIs(3));
  EXPECT_EQ(sent_packets[0].PayloadType(), kPayloadType);
  EXPECT_EQ(sent_packets[1].PayloadType(), kPayloadType);
  EXPECT_EQ(sent_packets[2].PayloadType(), kPayloadType2);

  // Verify that NACK is sent to the RTX payload type corresponding to the
  // payload type.
  rtcp::Nack nack1, nack2;
  nack1.SetMediaSsrc(kSsrc1);
  nack2.SetMediaSsrc(kSsrc2);
  nack1.SetPacketIds({rtp_sequence_numbers[0], rtp_sequence_numbers[1]});
  nack2.SetPacketIds({rtp_sequence_numbers[2]});
  Buffer nack_buffer1 = nack1.Build();
  Buffer nack_buffer2 = nack2.Build();

  std::vector<RtpPacket> sent_rtx_packets;
  EXPECT_CALL(test.transport(), SendRtp)
      .Times(3)
      .WillRepeatedly(
          [&](std::span<const uint8_t> packet, const PacketOptions& options) {
            RtpPacket& rtp_packet = sent_rtx_packets.emplace_back();
            EXPECT_TRUE(rtp_packet.Parse(packet));
            return true;
          });
  test.router()->DeliverRtcp(nack_buffer1);
  test.router()->DeliverRtcp(nack_buffer2);

  test.AdvanceTime(TimeDelta::Millis(33));

  ASSERT_THAT(sent_rtx_packets, SizeIs(3));
  EXPECT_EQ(sent_rtx_packets[0].PayloadType(), kPayloadType + 1);
  EXPECT_EQ(sent_rtx_packets[1].PayloadType(), kPayloadType + 1);
  EXPECT_EQ(sent_rtx_packets[2].PayloadType(), kPayloadType2 + 1);
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
          [&](std::span<const uint8_t> packet, const PacketOptions&) {
            EXPECT_TRUE(sent_packets.emplace_back(&extensions).Parse(packet));
            return true;
          });
  test.SetSending(true);

  EncodedImage key_frame_image;
  key_frame_image.set_frame_type(VideoFrameType::kVideoFrameKey);
  key_frame_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));
  CodecSpecificInfo key_frame_info;
  key_frame_info.codecType = VideoCodecType::kVideoCodecVP8;
  ASSERT_EQ(
      test.router()->OnEncodedImage(key_frame_image, &key_frame_info).error,
      EncodedImageCallback::Result::OK);

  EncodedImage delta_image;
  delta_image.set_frame_type(VideoFrameType::kVideoFrameDelta);
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
      .WillByDefault(
          [&](std::span<const uint8_t> packet, const PacketOptions& options) {
            sent_packets.emplace_back(&extensions);
            EXPECT_TRUE(sent_packets.back().Parse(packet));
            return true;
          });

  const uint8_t kPayload[1] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
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
  codec_specific.template_structure = std::nullopt;
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
      .WillByDefault(
          [&](std::span<const uint8_t> packet, const PacketOptions& options) {
            sent_packets.emplace_back(&extensions);
            EXPECT_TRUE(sent_packets.back().Parse(packet));
            return true;
          });

  const uint8_t kPayload[1] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
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
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameDelta);
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

TEST(RtpVideoSenderTest,
     SupportsDependencyDescriptorForH264NotProvidedByEncoder) {
  RtpVideoSenderTestFixture test({kSsrc1}, {}, kPayloadType, {});
  test.SetSending(true);

  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> sent_packets;
  EXPECT_CALL(test.transport(), SendRtp(_, _))
      .Times(2)
      .WillRepeatedly([&](std::span<const uint8_t> packet,
                          const PacketOptions& options) -> bool {
        sent_packets.emplace_back(&extensions);
        EXPECT_TRUE(sent_packets.back().Parse(packet));
        return true;
      });

  const uint8_t kPayload[1] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
  encoded_image._encodedWidth = 320;
  encoded_image._encodedHeight = 180;
  encoded_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));

  CodecSpecificInfo codec_specific;
  codec_specific.codecType = VideoCodecType::kVideoCodecH264;
  codec_specific.codecSpecific.H264.temporal_idx = kNoTemporalIdx;

  // Send two tiny images, each mapping to single RTP packet.
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);

  // Send in 2nd picture.
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameDelta);
  encoded_image.SetRtpTimestamp(3000);
  EXPECT_EQ(test.router()->OnEncodedImage(encoded_image, &codec_specific).error,
            EncodedImageCallback::Result::OK);

  test.AdvanceTime(TimeDelta::Millis(33));

  ASSERT_THAT(sent_packets, SizeIs(2));
  DependencyDescriptor dd_key;
  // Key frame should have attached structure.
  EXPECT_TRUE(sent_packets[0].GetExtension<RtpDependencyDescriptorExtension>(
      nullptr, &dd_key));
  EXPECT_THAT(dd_key.attached_structure, NotNull());
  // Delta frame does not have attached structure.
  DependencyDescriptor dd_delta;
  EXPECT_TRUE(sent_packets[1].GetExtension<RtpDependencyDescriptorExtension>(
      dd_key.attached_structure.get(), &dd_delta));
  EXPECT_THAT(dd_delta.attached_structure, IsNull());
}

TEST(RtpVideoSenderTest, GenerateDependecyDescriptorForGenericCodecs) {
  absl::string_view field_trials =
      "WebRTC-GenericCodecDependencyDescriptor/Enabled/";
  RtpVideoSenderTestFixture test({kSsrc1}, {}, kPayloadType, {}, field_trials);
  test.SetSending(true);

  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> sent_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault(
          [&](std::span<const uint8_t> packet, const PacketOptions& options) {
            sent_packets.emplace_back(&extensions);
            EXPECT_TRUE(sent_packets.back().Parse(packet));
            return true;
          });

  const uint8_t kPayload[1] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
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
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameDelta);
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
      .WillByDefault(
          [&](std::span<const uint8_t> packet, const PacketOptions& options) {
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
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
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
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
  codec_specific.template_structure = std::nullopt;
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
  scoped_refptr<MockFrameTransformer> transformer =
      make_ref_counted<MockFrameTransformer>();

  EXPECT_CALL(*transformer, RegisterTransformedFrameSinkCallback(_, kSsrc1));
  EXPECT_CALL(*transformer, RegisterTransformedFrameSinkCallback(_, kSsrc2));
  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {kRtxSsrc1, kRtxSsrc2},
                                 kPayloadType, {}, nullptr, transformer);

  EXPECT_CALL(*transformer, UnregisterTransformedFrameSinkCallback(kSsrc1));
  EXPECT_CALL(*transformer, UnregisterTransformedFrameSinkCallback(kSsrc2));
}

TEST(RtpVideoSenderTest, ReportsZeroBitratesBeforeFirstBitrateUpdate) {
  RtpVideoSenderTestFixture test({kSsrc1}, {}, kPayloadType, {});
  EXPECT_EQ(test.router()->GetPayloadBitrateBps(), 0u);
  EXPECT_EQ(test.router()->GetProtectionBitrateBps(), 0u);
}

TEST(RtpVideoSenderTest, OverheadIsSubtractedFromTargetBitrate) {
  absl::string_view field_trials =
      "WebRTC-Video-UseFrameRateForOverhead/Enabled/";

  // TODO(jakobi): RTP header size should not be hard coded.
  constexpr uint32_t kRtpHeaderSizeBytes = 20;
  constexpr uint32_t kTransportPacketOverheadBytes = 40;
  constexpr uint32_t kOverheadPerPacketBytes =
      kRtpHeaderSizeBytes + kTransportPacketOverheadBytes;
  RtpVideoSenderTestFixture test({kSsrc1}, {}, kPayloadType, {}, field_trials);
  test.SetSending(true);

  {
    test.router()->OnBitrateUpdated(
        CreateBitrateAllocationUpdate(
            300000, DataSize::Bytes(kTransportPacketOverheadBytes)),
        /*framerate*/ 15);
    // 1 packet per frame.
    EXPECT_EQ(test.router()->GetPayloadBitrateBps(),
              300000 - kOverheadPerPacketBytes * 8 * 30);
  }
  {
    test.router()->OnBitrateUpdated(
        CreateBitrateAllocationUpdate(
            150000, DataSize::Bytes(kTransportPacketOverheadBytes)),
        /*framerate*/ 15);
    // 1 packet per frame.
    EXPECT_EQ(test.router()->GetPayloadBitrateBps(),
              150000 - kOverheadPerPacketBytes * 8 * 15);
  }
  {
    test.router()->OnBitrateUpdated(
        CreateBitrateAllocationUpdate(
            1000000, DataSize::Bytes(kTransportPacketOverheadBytes)),
        /*framerate*/ 30);
    // 3 packets per frame.
    EXPECT_EQ(test.router()->GetPayloadBitrateBps(),
              1000000 - kOverheadPerPacketBytes * 8 * 30 * 3);
  }
}

TEST(RtpVideoSenderTest, ReservesBitrateForFecOnlyWhenAllowed) {
  constexpr uint32_t kTargetBitrateBps = 300'000;
  // The rate that the FEC controller leaves for the encoder.
  constexpr uint32_t kRateWithFecBps = 100'000;
  auto fec_controller = std::make_unique<NiceMock<MockFecController>>();
  uint32_t payload_bitrate_bps = 0;
  ON_CALL(*fec_controller, UpdateFecRates)
      .WillByDefault(
          DoAll(SaveArg<0>(&payload_bitrate_bps), Return(kRateWithFecBps)));
  RtpVideoSenderTestFixture test(
      {kSsrc1}, {}, kPayloadType, {}, /*frame_count_observer=*/nullptr,
      /*frame_transformer=*/nullptr, /*payload_types=*/{},
      /*field_trials=*/"", /*csrcs=*/{}, std::move(fec_controller));
  test.SetSending(true);
  const BitrateAllocationUpdate update =
      CreateBitrateAllocationUpdate(kTargetBitrateBps);

  test.router()->OnBitrateUpdated(update, /*framerate=*/30);
  EXPECT_EQ(test.router()->GetPayloadBitrateBps(), kRateWithFecBps);

  // SetFecAllowed() may be called on any thread. The value applies to bitrate
  // updates once it has been handed to the transport queue.
  test.router()->SetFecAllowed(false);
  test.AdvanceTime(TimeDelta::Zero());
  test.router()->OnBitrateUpdated(update, /*framerate=*/30);
  EXPECT_GT(payload_bitrate_bps, kRateWithFecBps);
  EXPECT_EQ(test.router()->GetPayloadBitrateBps(), payload_bitrate_bps);

  test.router()->SetFecAllowed(true);
  test.AdvanceTime(TimeDelta::Zero());
  test.router()->OnBitrateUpdated(update, /*framerate=*/30);
  EXPECT_EQ(test.router()->GetPayloadBitrateBps(), kRateWithFecBps);
}

TEST(RtpVideoSenderTest, ClearsPendingPacketsOnInactivation) {
  RtpVideoSenderTestFixture test({kSsrc1}, {kRtxSsrc1}, kPayloadType, {});
  test.SetSending(true);

  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> sent_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault(
          [&](std::span<const uint8_t> packet, const PacketOptions& options) {
            sent_packets.emplace_back(&extensions);
            EXPECT_TRUE(sent_packets.back().Parse(packet));
            return true;
          });

  // Set a very low bitrate.
  test.router()->OnBitrateUpdated(
      CreateBitrateAllocationUpdate(/*target_bitrate_bps=*/10'000),
      /*framerate=*/30);

  // Create and send a large keyframe.
  const size_t kImageSizeBytes = 10000;
  constexpr uint8_t kPayload[kImageSizeBytes] = {'a'};
  EncodedImage encoded_image;
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
  encoded_image.SetEncodedData(
      EncodedImageBuffer::Create(kPayload, sizeof(kPayload)));
  EXPECT_EQ(test.router()
                ->OnEncodedImage(encoded_image, /*codec_specific_info=*/nullptr)
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
                ->OnEncodedImage(encoded_image, /*codec_specific_info=*/nullptr)
                .error,
            EncodedImageCallback::Result::OK);
  test.AdvanceTime(TimeDelta::Millis(33));

  // Advance time, check we get new packets - but only for the second frame.
  EXPECT_FALSE(sent_packets.empty());
  EXPECT_NE(sent_packets[0].Timestamp(), first_frame_timestamp);
}

TEST(RtpVideoSenderTest,
     ClearsPendingPacketsOnInactivationWithLayerAllocation) {
  RtpVideoSenderTestFixture test({kSsrc1, kSsrc2}, {}, kPayloadType, {});
  test.SetSending(true);

  RtpHeaderExtensionMap extensions;
  extensions.Register<RtpDependencyDescriptorExtension>(
      kDependencyDescriptorExtensionId);
  std::vector<RtpPacket> sent_packets;
  ON_CALL(test.transport(), SendRtp)
      .WillByDefault(
          [&](std::span<const uint8_t> packet, const PacketOptions& options) {
            sent_packets.emplace_back(&extensions);
            EXPECT_TRUE(sent_packets.back().Parse(packet));
            return true;
          });

  // Set a very low bitrate.
  test.router()->OnBitrateUpdated(
      CreateBitrateAllocationUpdate(/*target_bitrate_bps=*/10'000),
      /*framerate=*/30);

  // Create and send a large keyframe.
  constexpr uint8_t kImage[10'000] = {};
  EncodedImage encoded_image;
  encoded_image.SetSimulcastIndex(0);
  encoded_image.SetRtpTimestamp(1);
  encoded_image.capture_time_ms_ = 2;
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
  encoded_image.SetEncodedData(
      EncodedImageBuffer::Create(kImage, std::size(kImage)));
  EXPECT_EQ(test.router()
                ->OnEncodedImage(encoded_image, /*codec_specific_info=*/nullptr)
                .error,
            EncodedImageCallback::Result::OK);

  // Advance time a small amount, check that sent data is only part of the
  // image.
  test.AdvanceTime(TimeDelta::Millis(5));
  DataSize transmitted_payload = DataSize::Zero();
  for (const RtpPacket& packet : sent_packets) {
    transmitted_payload += DataSize::Bytes(packet.payload_size());
    // Make sure we don't see the end of the frame.
    EXPECT_FALSE(packet.Marker());
  }
  EXPECT_GT(transmitted_payload, DataSize::Zero());
  EXPECT_LT(transmitted_payload, DataSize::Bytes(std::size(kImage)) / 3);

  // Record the RTP timestamp of the first frame.
  const uint32_t first_frame_timestamp = sent_packets[0].Timestamp();
  sent_packets.clear();

  // Disable the 1st sending module and advance time slightly. No packets should
  // be sent.
  test.router()->OnVideoLayersAllocationUpdated(
      {.active_spatial_layers = {{.rtp_stream_index = 1}}});
  test.AdvanceTime(TimeDelta::Millis(20));
  EXPECT_THAT(sent_packets, IsEmpty());

  // Reactive the send module - any packets should have been removed, so nothing
  // should be transmitted.
  test.router()->OnVideoLayersAllocationUpdated(
      {.active_spatial_layers = {{.rtp_stream_index = 0},
                                 {.rtp_stream_index = 1}}});
  test.AdvanceTime(TimeDelta::Millis(33));
  EXPECT_THAT(sent_packets, IsEmpty());

  // Send a new frame.
  encoded_image.SetRtpTimestamp(3);
  encoded_image.capture_time_ms_ = 4;
  EXPECT_EQ(test.router()
                ->OnEncodedImage(encoded_image, /*codec_specific_info=*/nullptr)
                .error,
            EncodedImageCallback::Result::OK);
  test.AdvanceTime(TimeDelta::Millis(33));

  // Advance time, check we get new packets - but only for the second frame.
  ASSERT_THAT(sent_packets, SizeIs(Ge(1)));
  EXPECT_NE(sent_packets[0].Timestamp(), first_frame_timestamp);
}

TEST(RtpVideoSenderTest, PostTaskRaceDoesNotLeadToDanglingPointer) {
  NiceMock<MockTransport> transport;
  NiceMock<MockRtcpIntraFrameObserver> encoder_feedback;

  GlobalSimulatedTimeController time_controller(Timestamp::Millis(1000000));
  Environment env = CreateTestEnvironment({.time = &time_controller});

  VideoSendStream::Config config(&transport);
  config.rtp.ssrcs = {kSsrc1};

  SendStatisticsProxy stats_proxy(
      env, config, VideoEncoderConfig::ContentType::kRealtimeVideo);

  BitrateConstraints bitrate_config = GetBitrateConfig();
  RtpTransportConfig transport_config{
      .env = env,
      .bitrate_config = bitrate_config,
      .worker_thread = time_controller.GetMainThread()};
  RtpTransportControllerSend transport_controller(transport_config);
  transport_controller.EnsureStarted();

  std::map<uint32_t, RtpState> suspended_ssrcs;
  std::map<uint32_t, RtpPayloadState> suspended_payload_states;

  auto router = std::make_unique<RtpVideoSender>(
      env, time_controller.GetMainThread(), suspended_ssrcs,
      suspended_payload_states, config.rtp, config.rtcp_report_interval_ms,
      &transport,
      CreateObservers(&encoder_feedback, &stats_proxy, &stats_proxy,
                      &stats_proxy, nullptr, &stats_proxy),
      &transport_controller, std::make_unique<FecControllerDefault>(env),
      nullptr, CryptoOptions{}, nullptr);

  router->SetSending(true);
  // Verify it's registered
  EXPECT_TRUE(
      transport_controller.packet_router()->SsrcOfFirstSender().has_value());

  // Trigger race:
  // 1. OnVideoLayersAllocationUpdated posts a task to re-register modules
  // because it sees active_ is true.
  VideoLayersAllocation allocation;
  allocation.active_spatial_layers.push_back({.rtp_stream_index = 0});
  router->OnVideoLayersAllocationUpdated(allocation);

  // 2. Immediately set active_ to false. This happens BEFORE the posted task
  // runs.
  router->SetSending(false);

  // 3. Let the posted task run. With the fix, it should return early and not
  // re-register the module because active_ is now false.
  time_controller.AdvanceTime(TimeDelta::Zero());

  // 4. Verify that PacketRouter does NOT have a sender.
  EXPECT_FALSE(
      transport_controller.packet_router()->SsrcOfFirstSender().has_value());

  // 5. Destroy the router.
  router.reset();

  // 6. Verify that PacketRouter still does not have a sender.
  EXPECT_FALSE(
      transport_controller.packet_router()->SsrcOfFirstSender().has_value());
}

}  // namespace webrtc
