/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/rtp_sender_video.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/memory/memory.h"
#include "api/call/transport.h"
#include "api/environment/environment.h"
#include "api/frame_transformer_factory.h"
#include "api/frame_transformer_interface.h"
#include "api/make_ref_counted.h"
#include "api/rtc_error.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_headers.h"
#include "api/scoped_refptr.h"
#include "api/sframe/sframe_types.h"
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/test/mock_frame_encryptor.h"
#include "api/test/mock_frame_transformer.h"
#include "api/transport/rtp/corruption_detection_message.h"
#include "api/transport/rtp/dependency_descriptor.h"
#include "api/units/data_rate.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/corruption_detection/frame_instrumentation_data.h"
#include "api/video/encoded_image.h"
#include "api/video/video_codec_constants.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_layers_allocation.h"
#include "api/video/video_rotation.h"
#include "api/video/video_timing.h"
#include "modules/rtp_rtcp/include/rtp_cvo.h"
#include "modules/rtp_rtcp/include/rtp_header_extension_map.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/corruption_detection_extension.h"
#include "modules/rtp_rtcp/source/rtcp_packet/nack.h"
#include "modules/rtp_rtcp/source/rtcp_packet/receiver_report.h"
#include "modules/rtp_rtcp/source/rtcp_packet/report_block.h"
#include "modules/rtp_rtcp/source/rtp_dependency_descriptor_extension.h"
#include "modules/rtp_rtcp/source/rtp_format_video_generic.h"
#include "modules/rtp_rtcp/source/rtp_generic_frame_descriptor.h"
#include "modules/rtp_rtcp/source/rtp_generic_frame_descriptor_extension.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_impl2.h"
#include "modules/rtp_rtcp/source/rtp_sender.h"
#include "modules/rtp_rtcp/source/rtp_video_layers_allocation_extension.h"
#include "modules/rtp_rtcp/source/sframe_descriptor.h"
#include "modules/sframe/mock_sframe_media_encryptor.h"
#include "modules/sframe/sframe_encryption_config.h"
#include "modules/sframe/sframe_encryptor.h"
#include "modules/video_coding/codecs/h264/include/h264_globals.h"
#include "modules/video_coding/codecs/vp8/include/vp8_globals.h"
#include "modules/video_coding/codecs/vp9/include/vp9_globals.h"
#include "rtc_base/checks.h"
#include "system_wrappers/include/clock.h"
#include "system_wrappers/include/ntp_time.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/time_controller/simulated_time_controller.h"

namespace webrtc {

namespace {

using ::testing::_;
using ::testing::ContainerEq;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::testing::Le;
using ::testing::NiceMock;
using ::testing::Not;
using ::testing::Optional;
using ::testing::Return;
using ::testing::ReturnArg;
using ::testing::SaveArg;
using ::testing::SizeIs;
using ::testing::WithArgs;

constexpr RtpHeaderExtensionId kAbsoluteSendTimeExtensionId(1);
constexpr RtpHeaderExtensionId kGenericDescriptorId(2);
constexpr RtpHeaderExtensionId kDependencyDescriptorId(3);
constexpr RtpHeaderExtensionId kTransmissionTimeOffsetExtensionId(4);
constexpr RtpHeaderExtensionId kTransportSequenceNumberExtensionId(5);
constexpr RtpHeaderExtensionId kVideoRotationExtensionId(6);
constexpr RtpHeaderExtensionId kVideoTimingExtensionId(7);
constexpr RtpHeaderExtensionId kAbsoluteCaptureTimeExtensionId(8);
constexpr RtpHeaderExtensionId kPlayoutDelayExtensionId(9);
constexpr RtpHeaderExtensionId kVideoLayersAllocationExtensionId(10);
constexpr RtpHeaderExtensionId kCorruptionDetectionExtensionId(11);

constexpr int kPayloadType = 100;
constexpr int kRedPayloadType = 101;
constexpr VideoCodecType kType = VideoCodecType::kVideoCodecGeneric;
constexpr uint32_t kRtpTimestamp = 10;
constexpr RtpTimestampInfo kTimestampInfo =
    RtpTimestampWithOffset{kRtpTimestamp};
constexpr RtpTimestampInfo kTimestampInfoPlusOne =
    RtpTimestampWithOffset{kRtpTimestamp + 1};
constexpr uint16_t kSeqNum = 33;
constexpr uint32_t kSsrc = 725242;
constexpr uint32_t kRtxSsrc = 912364;
constexpr int kMaxPacketLength = 1500;
constexpr Timestamp kStartTime = Timestamp::Millis(123456789);
constexpr TimeDelta kDefaultExpectedRetransmissionTime = TimeDelta::Millis(125);

class LoopbackTransportTest : public Transport {
 public:
  LoopbackTransportTest() {
    receivers_extensions_.Register<TransmissionOffset>(
        kTransmissionTimeOffsetExtensionId);
    receivers_extensions_.Register<AbsoluteSendTime>(
        kAbsoluteSendTimeExtensionId);
    receivers_extensions_.Register<TransportSequenceNumber>(
        kTransportSequenceNumberExtensionId);
    receivers_extensions_.Register<VideoOrientation>(kVideoRotationExtensionId);
    receivers_extensions_.Register<VideoTimingExtension>(
        kVideoTimingExtensionId);
    receivers_extensions_.Register<RtpGenericFrameDescriptorExtension00>(
        kGenericDescriptorId);
    receivers_extensions_.Register<RtpDependencyDescriptorExtension>(
        kDependencyDescriptorId);
    receivers_extensions_.Register<AbsoluteCaptureTimeExtension>(
        kAbsoluteCaptureTimeExtensionId);
    receivers_extensions_.Register<PlayoutDelayLimits>(
        kPlayoutDelayExtensionId);
    receivers_extensions_.Register<RtpVideoLayersAllocationExtension>(
        kVideoLayersAllocationExtensionId);
    receivers_extensions_.Register<CorruptionDetectionExtension>(
        kCorruptionDetectionExtensionId);
  }

  bool SendRtp(std::span<const uint8_t> data,
               const PacketOptions& /* options */) override {
    sent_packets_.push_back(RtpPacketReceived(&receivers_extensions_));
    EXPECT_TRUE(sent_packets_.back().Parse(data));
    return true;
  }
  bool SendRtcp(std::span<const uint8_t> /* data */,
                const PacketOptions& /* options */) override {
    return false;
  }
  const RtpPacketReceived& last_sent_packet() { return sent_packets_.back(); }
  int packets_sent() { return sent_packets_.size(); }
  const std::vector<RtpPacketReceived>& sent_packets() const {
    return sent_packets_;
  }

 private:
  RtpHeaderExtensionMap receivers_extensions_;
  std::vector<RtpPacketReceived> sent_packets_;
};

struct TestRtpSenderOptions {
  bool raw_packetization = false;
  bool sframe_required_by_negotiation = false;
  std::optional<SframeEncryptionConfig> sframe_encryption_config;
  std::optional<int> red_payload_type;
  FrameEncryptorInterface* frame_encryptor = nullptr;
  scoped_refptr<FrameTransformerInterface> transformer;
};

class TestRtpSenderVideo : public RTPSenderVideo {
 public:
  TestRtpSenderVideo(const Environment& env,
                     RTPSender* absl_nonnull rtp_sender,
                     TestRtpSenderOptions options = {})
      : RTPSenderVideo([&] {
          Config config;
          config.clock = &env.clock();
          config.rtp_sender = rtp_sender;
          config.field_trials = &env.field_trials();
          config.task_queue_factory = &env.task_queue_factory();
          config.raw_packetization = options.raw_packetization;
          config.frame_transformer = std::move(options.transformer);
          config.frame_encryptor = options.frame_encryptor;
          config.sframe_required_by_negotiation =
              options.sframe_required_by_negotiation;
          config.sframe_encryption_config =
              std::move(options.sframe_encryption_config);
          config.red_payload_type = options.red_payload_type;
          return config;
        }()) {}
  ~TestRtpSenderVideo() override = default;

  bool AllowRetransmission(const RTPVideoHeader& header,
                           int32_t retransmission_settings,
                           TimeDelta expected_retransmission_time) {
    return RTPSenderVideo::AllowRetransmission(GetTemporalId(header),
                                               retransmission_settings,
                                               expected_retransmission_time);
  }
};

class RtpSenderVideoTest : public ::testing::Test {
 public:
  RtpSenderVideoTest()
      : time_controller_(kStartTime),
        env_(CreateTestEnvironment({.time = &time_controller_})),
        rtp_module_(ModuleRtpRtcpImpl2::CreateSendModule(
            env_,
            {.outgoing_transport = &transport_,
             .local_media_ssrc = kSsrc,
             .rtx_send_ssrc = kRtxSsrc,
             .rtp_state = {{.sequence_number = kSeqNum, .start_timestamp = 0}},
             .rid = "myrid"})) {}

  TestRtpSenderVideo Create(TestRtpSenderOptions options = {}) {
    return TestRtpSenderVideo(env_, rtp_module_->RtpSender(), options);
  }

 protected:
  GlobalSimulatedTimeController time_controller_;
  const Environment env_;
  LoopbackTransportTest transport_;
  const std::unique_ptr<ModuleRtpRtcpImpl2> rtp_module_;
};

TEST_F(RtpSenderVideoTest, KeyFrameHasCVO) {
  uint8_t kFrame[kMaxPacketLength];
  rtp_module_->RegisterRtpHeaderExtension(VideoOrientation::Uri(),
                                          kVideoRotationExtensionId);
  TestRtpSenderVideo rtp_sender_video = Create();

  RTPVideoHeader hdr;
  hdr.rotation = kVideoRotation_0;
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  VideoRotation rotation;
  EXPECT_TRUE(
      transport_.last_sent_packet().GetExtension<VideoOrientation>(&rotation));
  EXPECT_EQ(kVideoRotation_0, rotation);
}

TEST_F(RtpSenderVideoTest, TimingFrameHasPacketizationTimstampSet) {
  uint8_t kFrame[kMaxPacketLength];
  const TimeDelta kPacketizationTime = TimeDelta::Millis(100);
  const int64_t kEncodeStartDeltaMs = 10;
  const int64_t kEncodeFinishDeltaMs = 50;
  rtp_module_->RegisterRtpHeaderExtension(VideoTimingExtension::Uri(),
                                          kVideoTimingExtensionId);
  TestRtpSenderVideo rtp_sender_video = Create();

  const Timestamp kCaptureTimestamp = env_.clock().CurrentTime();

  RTPVideoHeader hdr;
  hdr.video_timing.flags = VideoSendTiming::kTriggeredByTimer;
  hdr.video_timing.encode_start_delta_ms = kEncodeStartDeltaMs;
  hdr.video_timing.encode_finish_delta_ms = kEncodeFinishDeltaMs;

  time_controller_.AdvanceTime(kPacketizationTime);
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  rtp_sender_video.SendVideoFrame(kPayloadType, kType, kTimestampInfo,
                                  kCaptureTimestamp, kFrame, sizeof(kFrame),
                                  hdr, kDefaultExpectedRetransmissionTime, {});
  VideoSendTiming timing;
  EXPECT_TRUE(transport_.last_sent_packet().GetExtension<VideoTimingExtension>(
      &timing));
  EXPECT_EQ(kPacketizationTime.ms(), timing.packetization_finish_delta_ms);
  EXPECT_EQ(kEncodeStartDeltaMs, timing.encode_start_delta_ms);
  EXPECT_EQ(kEncodeFinishDeltaMs, timing.encode_finish_delta_ms);
}

TEST_F(RtpSenderVideoTest,
       WriteCorruptionExtensionIfHeaderContainsFrameInstrumentationData) {
  uint8_t kFrame[kMaxPacketLength];
  rtp_module_->RegisterRtpHeaderExtension(CorruptionDetectionExtension::Uri(),
                                          kCorruptionDetectionExtensionId);
  TestRtpSenderVideo rtp_sender_video = Create();

  RTPVideoHeader hdr;
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  FrameInstrumentationData data;
  data.SetSequenceIndex(130);  // 128 + 2
  data.SetStdDev(2.0);
  data.SetLumaErrorThreshold(3);
  data.SetChromaErrorThreshold(2);
  data.SetSampleValues({12.0, 12.0, 12.0, 12.0, 12.0, 12.0, 12.0, 12.0});
  hdr.frame_instrumentation_data = data;
  CorruptionDetectionMessage message;

  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  // Only written on last packet.
  for (const RtpPacketReceived& packet : transport_.sent_packets()) {
    if (&packet == &transport_.last_sent_packet()) {
      EXPECT_TRUE(transport_.last_sent_packet()
                      .GetExtension<CorruptionDetectionExtension>(&message));
    } else {
      EXPECT_FALSE(packet.HasExtension<CorruptionDetectionExtension>());
    }
  }
  EXPECT_EQ(message.sequence_index(), 2);
  EXPECT_FALSE(message.interpret_sequence_index_as_most_significant_bits());
  EXPECT_NEAR(message.std_dev(), 2.0392156862745097, 0.041);  // ~2%
  EXPECT_EQ(message.luma_error_threshold(), 3);
  EXPECT_EQ(message.chroma_error_threshold(), 2);
  EXPECT_THAT(message.sample_values(),
              ElementsAre(12.0, 12.0, 12.0, 12.0, 12.0, 12.0, 12.0, 12.0));
}

TEST_F(RtpSenderVideoTest,
       WriteCorruptionExtensionIfHeaderContainsFrameInstrumentationSyncData) {
  uint8_t kFrame[kMaxPacketLength];
  rtp_module_->RegisterRtpHeaderExtension(CorruptionDetectionExtension::Uri(),
                                          kCorruptionDetectionExtensionId);
  TestRtpSenderVideo rtp_sender_video = Create();

  RTPVideoHeader hdr;
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  // Send data with sequence index divisible by 2^7 and no sample values in
  // order create a sync message with upper bits set.
  hdr.frame_instrumentation_data.emplace().SetSequenceIndex(128);
  CorruptionDetectionMessage message;

  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  // Only written on last packet.
  for (const RtpPacketReceived& packet : transport_.sent_packets()) {
    if (&packet == &transport_.last_sent_packet()) {
      EXPECT_TRUE(transport_.last_sent_packet()
                      .GetExtension<CorruptionDetectionExtension>(&message));
    } else {
      EXPECT_FALSE(packet.HasExtension<CorruptionDetectionExtension>());
    }
  }
  EXPECT_EQ(message.sequence_index(), 1);
  EXPECT_TRUE(message.interpret_sequence_index_as_most_significant_bits());
  EXPECT_DOUBLE_EQ(message.std_dev(), 0.0);
  EXPECT_EQ(message.luma_error_threshold(), 0);
  EXPECT_EQ(message.chroma_error_threshold(), 0);
  EXPECT_THAT(message.sample_values(), IsEmpty());
}

TEST_F(RtpSenderVideoTest, DeltaFrameHasCVOWhenChanged) {
  uint8_t kFrame[kMaxPacketLength];
  rtp_module_->RegisterRtpHeaderExtension(VideoOrientation::Uri(),
                                          kVideoRotationExtensionId);
  TestRtpSenderVideo rtp_sender_video = Create();

  RTPVideoHeader hdr;
  hdr.rotation = kVideoRotation_90;
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  EXPECT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {}));

  hdr.rotation = kVideoRotation_0;
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  EXPECT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfoPlusOne, env_.clock().CurrentTime(),
      kFrame, sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {}));

  VideoRotation rotation;
  EXPECT_TRUE(
      transport_.last_sent_packet().GetExtension<VideoOrientation>(&rotation));
  EXPECT_EQ(kVideoRotation_0, rotation);
}

TEST_F(RtpSenderVideoTest, DeltaFrameHasCVOWhenNonZero) {
  uint8_t kFrame[kMaxPacketLength];
  rtp_module_->RegisterRtpHeaderExtension(VideoOrientation::Uri(),
                                          kVideoRotationExtensionId);
  TestRtpSenderVideo rtp_sender_video = Create();

  RTPVideoHeader hdr;
  hdr.rotation = kVideoRotation_90;
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  EXPECT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {}));

  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  EXPECT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfoPlusOne, env_.clock().CurrentTime(),
      kFrame, sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {}));

  VideoRotation rotation;
  EXPECT_TRUE(
      transport_.last_sent_packet().GetExtension<VideoOrientation>(&rotation));
  EXPECT_EQ(kVideoRotation_90, rotation);
}

// Make sure rotation is parsed correctly when the Camera (C) and Flip (F) bits
// are set in the CVO byte.
TEST_F(RtpSenderVideoTest, SendVideoFrameWithCameraAndFlipCVO) {
  // Test extracting rotation when Camera (C) and Flip (F) bits are zero.
  EXPECT_EQ(kVideoRotation_0, ConvertCVOByteToVideoRotation(0));
  EXPECT_EQ(kVideoRotation_90, ConvertCVOByteToVideoRotation(1));
  EXPECT_EQ(kVideoRotation_180, ConvertCVOByteToVideoRotation(2));
  EXPECT_EQ(kVideoRotation_270, ConvertCVOByteToVideoRotation(3));
  // Test extracting rotation when Camera (C) and Flip (F) bits are set.
  const int flip_bit = 1 << 2;
  const int camera_bit = 1 << 3;
  EXPECT_EQ(kVideoRotation_0,
            ConvertCVOByteToVideoRotation(flip_bit | camera_bit | 0));
  EXPECT_EQ(kVideoRotation_90,
            ConvertCVOByteToVideoRotation(flip_bit | camera_bit | 1));
  EXPECT_EQ(kVideoRotation_180,
            ConvertCVOByteToVideoRotation(flip_bit | camera_bit | 2));
  EXPECT_EQ(kVideoRotation_270,
            ConvertCVOByteToVideoRotation(flip_bit | camera_bit | 3));
}

TEST_F(RtpSenderVideoTest, RetransmissionTypesGeneric) {
  TestRtpSenderVideo rtp_sender_video = Create();
  RTPVideoHeader header;
  header.codec = kVideoCodecGeneric;

  EXPECT_FALSE(rtp_sender_video.AllowRetransmission(
      header, kRetransmitOff, kDefaultExpectedRetransmissionTime));
  EXPECT_TRUE(rtp_sender_video.AllowRetransmission(
      header, kRetransmitBaseLayer, kDefaultExpectedRetransmissionTime));
  EXPECT_TRUE(rtp_sender_video.AllowRetransmission(
      header, kRetransmitHigherLayers, kDefaultExpectedRetransmissionTime));
  EXPECT_TRUE(rtp_sender_video.AllowRetransmission(
      header, kConditionallyRetransmitHigherLayers,
      kDefaultExpectedRetransmissionTime));
}

TEST_F(RtpSenderVideoTest, RetransmissionTypesH264) {
  TestRtpSenderVideo rtp_sender_video = Create();
  RTPVideoHeader header;
  header.video_type_header.emplace<RTPVideoHeaderH264>().packetization_mode =
      H264PacketizationMode::NonInterleaved;
  header.codec = kVideoCodecH264;

  EXPECT_FALSE(rtp_sender_video.AllowRetransmission(
      header, kRetransmitOff, kDefaultExpectedRetransmissionTime));
  EXPECT_TRUE(rtp_sender_video.AllowRetransmission(
      header, kRetransmitBaseLayer, kDefaultExpectedRetransmissionTime));
  EXPECT_TRUE(rtp_sender_video.AllowRetransmission(
      header, kRetransmitHigherLayers, kDefaultExpectedRetransmissionTime));
  EXPECT_TRUE(rtp_sender_video.AllowRetransmission(
      header, kConditionallyRetransmitHigherLayers,
      kDefaultExpectedRetransmissionTime));
}

TEST_F(RtpSenderVideoTest, RetransmissionTypesVP8BaseLayer) {
  TestRtpSenderVideo rtp_sender_video = Create();
  RTPVideoHeader header;
  header.codec = kVideoCodecVP8;
  auto& vp8_header = header.video_type_header.emplace<RTPVideoHeaderVP8>();
  vp8_header.temporalIdx = 0;

  EXPECT_FALSE(rtp_sender_video.AllowRetransmission(
      header, kRetransmitOff, kDefaultExpectedRetransmissionTime));
  EXPECT_TRUE(rtp_sender_video.AllowRetransmission(
      header, kRetransmitBaseLayer, kDefaultExpectedRetransmissionTime));
  EXPECT_FALSE(rtp_sender_video.AllowRetransmission(
      header, kRetransmitHigherLayers, kDefaultExpectedRetransmissionTime));
  EXPECT_TRUE(rtp_sender_video.AllowRetransmission(
      header, kRetransmitHigherLayers | kRetransmitBaseLayer,
      kDefaultExpectedRetransmissionTime));
  EXPECT_FALSE(rtp_sender_video.AllowRetransmission(
      header, kConditionallyRetransmitHigherLayers,
      kDefaultExpectedRetransmissionTime));
  EXPECT_TRUE(rtp_sender_video.AllowRetransmission(
      header, kRetransmitBaseLayer | kConditionallyRetransmitHigherLayers,
      kDefaultExpectedRetransmissionTime));
}

TEST_F(RtpSenderVideoTest, RetransmissionTypesVP8HigherLayers) {
  TestRtpSenderVideo rtp_sender_video = Create();
  RTPVideoHeader header;
  header.codec = kVideoCodecVP8;

  auto& vp8_header = header.video_type_header.emplace<RTPVideoHeaderVP8>();
  for (size_t tid = 1; tid <= kMaxTemporalStreams; ++tid) {
    vp8_header.temporalIdx = tid;

    EXPECT_FALSE(rtp_sender_video.AllowRetransmission(
        header, kRetransmitOff, kDefaultExpectedRetransmissionTime));
    EXPECT_FALSE(rtp_sender_video.AllowRetransmission(
        header, kRetransmitBaseLayer, kDefaultExpectedRetransmissionTime));
    EXPECT_TRUE(rtp_sender_video.AllowRetransmission(
        header, kRetransmitHigherLayers, kDefaultExpectedRetransmissionTime));
    EXPECT_TRUE(rtp_sender_video.AllowRetransmission(
        header, kRetransmitHigherLayers | kRetransmitBaseLayer,
        kDefaultExpectedRetransmissionTime));
  }
}

TEST_F(RtpSenderVideoTest, RetransmissionTypesVP9) {
  TestRtpSenderVideo rtp_sender_video = Create();
  RTPVideoHeader header;
  header.codec = kVideoCodecVP9;

  auto& vp9_header = header.video_type_header.emplace<RTPVideoHeaderVP9>();
  for (size_t tid = 1; tid <= kMaxTemporalStreams; ++tid) {
    vp9_header.temporal_idx = tid;

    EXPECT_FALSE(rtp_sender_video.AllowRetransmission(
        header, kRetransmitOff, kDefaultExpectedRetransmissionTime));
    EXPECT_FALSE(rtp_sender_video.AllowRetransmission(
        header, kRetransmitBaseLayer, kDefaultExpectedRetransmissionTime));
    EXPECT_TRUE(rtp_sender_video.AllowRetransmission(
        header, kRetransmitHigherLayers, kDefaultExpectedRetransmissionTime));
    EXPECT_TRUE(rtp_sender_video.AllowRetransmission(
        header, kRetransmitHigherLayers | kRetransmitBaseLayer,
        kDefaultExpectedRetransmissionTime));
  }
}

TEST_F(RtpSenderVideoTest, ConditionalRetransmit) {
  TestRtpSenderVideo rtp_sender_video = Create();
  constexpr TimeDelta kFrameInterval = TimeDelta::Millis(33);
  constexpr TimeDelta kRtt = (kFrameInterval * 3) / 2;
  const uint8_t kSettings =
      kRetransmitBaseLayer | kConditionallyRetransmitHigherLayers;

  // Insert VP8 frames for all temporal layers, but stop before the final index.
  RTPVideoHeader header;
  header.codec = kVideoCodecVP8;

  // Fill averaging window to prevent rounding errors.
  constexpr int kNumRepetitions =
      RTPSenderVideo::kTLRateWindowSize / kFrameInterval;
  constexpr std::array kPattern = {0, 2, 1, 2};
  auto& vp8_header = header.video_type_header.emplace<RTPVideoHeaderVP8>();
  for (size_t i = 0; i < kPattern.size() * kNumRepetitions; ++i) {
    vp8_header.temporalIdx = kPattern[i % kPattern.size()];
    rtp_sender_video.AllowRetransmission(header, kSettings, kRtt);
    time_controller_.AdvanceTime(kFrameInterval);
  }

  // Since we're at the start of the pattern, the next expected frame in TL0 is
  // right now. We will wait at most one expected retransmission time before
  // acknowledging that it did not arrive, which means this frame and the next
  // will not be retransmitted.
  vp8_header.temporalIdx = 1;
  EXPECT_FALSE(rtp_sender_video.AllowRetransmission(header, kSettings, kRtt));
  time_controller_.AdvanceTime(kFrameInterval);
  EXPECT_FALSE(rtp_sender_video.AllowRetransmission(header, kSettings, kRtt));
  time_controller_.AdvanceTime(kFrameInterval);

  // The TL0 frame did not arrive. So allow retransmission.
  EXPECT_TRUE(rtp_sender_video.AllowRetransmission(header, kSettings, kRtt));
  time_controller_.AdvanceTime(kFrameInterval);

  // Insert a frame for TL2. We just had frame in TL1, so the next one there is
  // in three frames away. TL0 is still too far in the past. So, allow
  // retransmission.
  vp8_header.temporalIdx = 2;
  EXPECT_TRUE(rtp_sender_video.AllowRetransmission(header, kSettings, kRtt));
  time_controller_.AdvanceTime(kFrameInterval);

  // Another TL2, next in TL1 is two frames away. Allow again.
  EXPECT_TRUE(rtp_sender_video.AllowRetransmission(header, kSettings, kRtt));
  time_controller_.AdvanceTime(kFrameInterval);

  // Yet another TL2, next in TL1 is now only one frame away, so don't store
  // for retransmission.
  EXPECT_FALSE(rtp_sender_video.AllowRetransmission(header, kSettings, kRtt));
}

TEST_F(RtpSenderVideoTest, ConditionalRetransmitLimit) {
  TestRtpSenderVideo rtp_sender_video = Create();
  constexpr TimeDelta kFrameInterval = TimeDelta::Millis(200);
  constexpr TimeDelta kRtt = (kFrameInterval * 3) / 2;
  const int32_t kSettings =
      kRetransmitBaseLayer | kConditionallyRetransmitHigherLayers;

  // Insert VP8 frames for all temporal layers, but stop before the final index.
  RTPVideoHeader header;
  header.codec = kVideoCodecVP8;

  // Fill averaging window to prevent rounding errors.
  constexpr int kNumRepetitions =
      RTPSenderVideo::kTLRateWindowSize / kFrameInterval;
  constexpr std::array kPattern = {0, 2, 2, 2};
  auto& vp8_header = header.video_type_header.emplace<RTPVideoHeaderVP8>();
  for (size_t i = 0; i < kPattern.size() * kNumRepetitions; ++i) {
    vp8_header.temporalIdx = kPattern[i % kPattern.size()];

    rtp_sender_video.AllowRetransmission(header, kSettings, kRtt);
    time_controller_.AdvanceTime(kFrameInterval);
  }

  // Since we're at the start of the pattern, the next expected frame will be
  // right now in TL0. Put it in TL1 instead. Regular rules would dictate that
  // we don't store for retransmission because we expect a frame in a lower
  // layer, but that last frame in TL1 was a long time ago in absolute terms,
  // so allow retransmission anyway.
  vp8_header.temporalIdx = 1;
  EXPECT_TRUE(rtp_sender_video.AllowRetransmission(header, kSettings, kRtt));
}

TEST_F(RtpSenderVideoTest,
       ReservesEnoughSpaceForRtxPacketWhenMidAndRsidAreRegistered) {
  constexpr int kMediaPayloadId = 100;
  constexpr int kRtxPayloadId = 101;
  constexpr size_t kMaxPacketSize = 1'000;

  // Unlike `rtp_module_`, this module sends RTX.
  const std::unique_ptr<ModuleRtpRtcpImpl2> rtp_module =
      ModuleRtpRtcpImpl2::CreateSendModule(
          env_, {.outgoing_transport = &transport_,
                 .local_media_ssrc = kSsrc,
                 .rtx_send_ssrc = kRtxSsrc,
                 .rtx_mode = kRtxRetransmitted,
                 .rtx_payload_types = {{kMediaPayloadId, kRtxPayloadId}},
                 .rtp_state = RtpState{.start_timestamp = 0},
                 .mid = "long_mid",
                 .rid = "rid"});
  rtp_module->SetSequenceNumber(kSeqNum);
  TestRtpSenderVideo rtp_sender_video(env_, rtp_module->RtpSender());

  rtp_module->SetMaxRtpPacketSize(kMaxPacketSize);
  rtp_module->RegisterRtpHeaderExtension(RtpMid::Uri(),
                                         RtpHeaderExtensionId(1));
  rtp_module->RegisterRtpHeaderExtension(RtpStreamId::Uri(),
                                         RtpHeaderExtensionId(2));
  rtp_module->RegisterRtpHeaderExtension(RepairedRtpStreamId::Uri(),
                                         RtpHeaderExtensionId(3));
  rtp_module->RegisterRtpHeaderExtension(AbsoluteSendTime::Uri(),
                                         RtpHeaderExtensionId(4));
  rtp_module->SetStorePacketsStatus(/*enable=*/true, 10);

  RTPVideoHeader header;
  header.codec = kVideoCodecVP8;
  header.frame_type = VideoFrameType::kVideoFrameDelta;
  auto& vp8_header = header.video_type_header.emplace<RTPVideoHeaderVP8>();
  vp8_header.temporalIdx = 0;

  uint8_t kPayload[kMaxPacketSize] = {};
  EXPECT_TRUE(rtp_sender_video.SendVideoFrame(
      kMediaPayloadId, /*codec_type=*/kVideoCodecVP8,
      RtpTimestampInfo(RtpTimestampWithOffset{0}),
      /*capture_time=*/Timestamp::Seconds(1), kPayload, sizeof(kPayload),
      header,
      /*expected_retransmission_time=*/TimeDelta::PlusInfinity(),
      /*csrcs=*/{}));
  ASSERT_THAT(transport_.sent_packets(), Not(IsEmpty()));
  // Ack media ssrc, but not rtx ssrc.
  rtcp::ReceiverReport rr;
  rtcp::ReportBlock rb;
  rb.SetMediaSsrc(kSsrc);
  rb.SetExtHighestSeqNum(transport_.last_sent_packet().SequenceNumber());
  rr.AddReportBlock(rb);
  rtp_module->IncomingRtcpPacket(rr.Build());

  // Test for various frame size close to `kMaxPacketSize` to catch edge cases
  // when rtx packet barely fit.
  for (size_t frame_size = 800; frame_size < kMaxPacketSize; ++frame_size) {
    SCOPED_TRACE(frame_size);
    std::span<const uint8_t> payload(kPayload, frame_size);

    EXPECT_TRUE(rtp_sender_video.SendVideoFrame(
        kMediaPayloadId, /*codec_type=*/kVideoCodecVP8,
        RtpTimestampInfo(RtpTimestampWithOffset{0}),
        /*capture_time=*/Timestamp::Seconds(1), payload, frame_size, header,
        /*expected_retransmission_time=*/TimeDelta::Seconds(1), /*csrcs=*/{}));
    const RtpPacketReceived& media_packet = transport_.last_sent_packet();
    EXPECT_EQ(media_packet.Ssrc(), kSsrc);

    rtcp::Nack nack;
    nack.SetMediaSsrc(kSsrc);
    nack.SetPacketIds({media_packet.SequenceNumber()});
    rtp_module->IncomingRtcpPacket(nack.Build());

    const RtpPacketReceived& rtx_packet = transport_.last_sent_packet();
    EXPECT_EQ(rtx_packet.Ssrc(), kRtxSsrc);
    EXPECT_LE(rtx_packet.size(), kMaxPacketSize);
  }
}

TEST_F(RtpSenderVideoTest, AppliesChangedMaxPacketSizeToNextFrame) {
  constexpr size_t kInitialMaxPacketSize = 1'200;
  constexpr size_t kReducedMaxPacketSize = 600;
  const uint8_t kFrame[2 * kInitialMaxPacketSize] = {};
  RTPVideoHeader header;
  header.frame_type = VideoFrameType::kVideoFrameKey;

  rtp_module_->SetMaxRtpPacketSize(kInitialMaxPacketSize);
  TestRtpSenderVideo rtp_sender_video = Create();

  ASSERT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), header, kDefaultExpectedRetransmissionTime, {}));
  const size_t num_initial_packets = transport_.sent_packets().size();
  EXPECT_THAT(transport_.sent_packets(),
              Each(SizeIs(Le(kInitialMaxPacketSize))));

  // All packets of the next frame are limited by the new max packet size.
  rtp_module_->SetMaxRtpPacketSize(kReducedMaxPacketSize);
  header.frame_type = VideoFrameType::kVideoFrameDelta;
  ASSERT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfoPlusOne, env_.clock().CurrentTime(),
      kFrame, sizeof(kFrame), header, kDefaultExpectedRetransmissionTime, {}));
  std::span<const RtpPacketReceived> reduced_packets =
      std::span(transport_.sent_packets()).subspan(num_initial_packets);
  EXPECT_GT(reduced_packets.size(), num_initial_packets);
  EXPECT_THAT(reduced_packets, Each(SizeIs(Le(kReducedMaxPacketSize))));
}

TEST_F(RtpSenderVideoTest, SendsDependencyDescriptorWhenVideoStructureIsSet) {
  const int64_t kFrameId = 100000;
  uint8_t kFrame[100];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpDependencyDescriptorExtension::Uri(), kDependencyDescriptorId);
  FrameDependencyStructure video_structure;
  video_structure.num_decode_targets = 2;
  video_structure.templates = {
      FrameDependencyTemplate().S(0).T(0).Dtis("SS"),
      FrameDependencyTemplate().S(1).T(0).Dtis("-S"),
      FrameDependencyTemplate().S(1).T(1).Dtis("-D"),
  };
  TestRtpSenderVideo rtp_sender_video = Create();
  rtp_sender_video.SetVideoStructure(&video_structure);

  // Send key frame.
  RTPVideoHeader hdr;
  RTPVideoHeader::GenericDescriptorInfo& generic = hdr.generic.emplace();
  generic.frame_id = kFrameId;
  generic.temporal_index = 0;
  generic.spatial_index = 0;
  generic.decode_target_indications = {DecodeTargetIndication::kSwitch,
                                       DecodeTargetIndication::kSwitch};
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  ASSERT_EQ(transport_.packets_sent(), 1);
  DependencyDescriptor descriptor_key;
  ASSERT_TRUE(transport_.last_sent_packet()
                  .GetExtension<RtpDependencyDescriptorExtension>(
                      nullptr, &descriptor_key));
  ASSERT_TRUE(descriptor_key.attached_structure);
  EXPECT_EQ(descriptor_key.attached_structure->num_decode_targets, 2);
  EXPECT_THAT(descriptor_key.attached_structure->templates, SizeIs(3));
  EXPECT_EQ(descriptor_key.frame_number, kFrameId & 0xFFFF);
  EXPECT_EQ(descriptor_key.frame_dependencies.spatial_id, 0);
  EXPECT_EQ(descriptor_key.frame_dependencies.temporal_id, 0);
  EXPECT_EQ(descriptor_key.frame_dependencies.decode_target_indications,
            generic.decode_target_indications);
  EXPECT_THAT(descriptor_key.frame_dependencies.frame_diffs, IsEmpty());

  // Send delta frame.
  generic.frame_id = kFrameId + 1;
  generic.temporal_index = 1;
  generic.spatial_index = 1;
  generic.dependencies = {kFrameId, kFrameId - 500};
  generic.decode_target_indications = {DecodeTargetIndication::kNotPresent,
                                       DecodeTargetIndication::kRequired};
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  EXPECT_EQ(transport_.packets_sent(), 2);
  DependencyDescriptor descriptor_delta;
  ASSERT_TRUE(
      transport_.last_sent_packet()
          .GetExtension<RtpDependencyDescriptorExtension>(
              descriptor_key.attached_structure.get(), &descriptor_delta));
  EXPECT_EQ(descriptor_delta.attached_structure, nullptr);
  EXPECT_EQ(descriptor_delta.frame_number, (kFrameId + 1) & 0xFFFF);
  EXPECT_EQ(descriptor_delta.frame_dependencies.spatial_id, 1);
  EXPECT_EQ(descriptor_delta.frame_dependencies.temporal_id, 1);
  EXPECT_EQ(descriptor_delta.frame_dependencies.decode_target_indications,
            generic.decode_target_indications);
  EXPECT_THAT(descriptor_delta.frame_dependencies.frame_diffs,
              ElementsAre(1, 501));
}

TEST_F(RtpSenderVideoTest,
       SkipsDependencyDescriptorOnDeltaFrameWhenFailedToAttachToKeyFrame) {
  const int64_t kFrameId = 100000;
  uint8_t kFrame[100];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpDependencyDescriptorExtension::Uri(), kDependencyDescriptorId);
  rtp_module_->SetExtmapAllowMixed(false);
  FrameDependencyStructure video_structure;
  video_structure.num_decode_targets = 2;
  // Use many templates so that key dependency descriptor would be too large
  // to fit into 16 bytes (max size of one byte header rtp header extension)
  video_structure.templates = {
      FrameDependencyTemplate().S(0).T(0).Dtis("SS"),
      FrameDependencyTemplate().S(1).T(0).Dtis("-S"),
      FrameDependencyTemplate().S(1).T(1).Dtis("-D").FrameDiffs({1, 2, 3, 4}),
      FrameDependencyTemplate().S(1).T(1).Dtis("-D").FrameDiffs({2, 3, 4, 5}),
      FrameDependencyTemplate().S(1).T(1).Dtis("-D").FrameDiffs({3, 4, 5, 6}),
      FrameDependencyTemplate().S(1).T(1).Dtis("-D").FrameDiffs({4, 5, 6, 7}),
  };
  TestRtpSenderVideo rtp_sender_video = Create();
  rtp_sender_video.SetVideoStructure(&video_structure);

  // Send key frame.
  RTPVideoHeader hdr;
  RTPVideoHeader::GenericDescriptorInfo& generic = hdr.generic.emplace();
  generic.frame_id = kFrameId;
  generic.temporal_index = 0;
  generic.spatial_index = 0;
  generic.decode_target_indications = {DecodeTargetIndication::kSwitch,
                                       DecodeTargetIndication::kSwitch};
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  ASSERT_EQ(transport_.packets_sent(), 1);
  DependencyDescriptor descriptor_key;
  ASSERT_FALSE(transport_.last_sent_packet()
                   .HasExtension<RtpDependencyDescriptorExtension>());

  // Send delta frame.
  generic.frame_id = kFrameId + 1;
  generic.temporal_index = 1;
  generic.spatial_index = 1;
  generic.dependencies = {kFrameId, kFrameId - 500};
  generic.decode_target_indications = {DecodeTargetIndication::kNotPresent,
                                       DecodeTargetIndication::kRequired};
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  EXPECT_EQ(transport_.packets_sent(), 2);
  EXPECT_FALSE(transport_.last_sent_packet()
                   .HasExtension<RtpDependencyDescriptorExtension>());
}

// A delta frame whose dependencies the dependency descriptor can't describe,
// e.g. because it references a frame more than 4096 frames back, is dropped.
// The dependency descriptor stays enabled for the frames that follow.
TEST_F(RtpSenderVideoTest,
       DropsDeltaFrameThatDependencyDescriptorCannotDescribe) {
  const int64_t kFrameId = 100000;
  uint8_t kFrame[100];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpDependencyDescriptorExtension::Uri(), kDependencyDescriptorId);
  FrameDependencyStructure video_structure;
  video_structure.num_decode_targets = 1;
  video_structure.templates = {
      FrameDependencyTemplate().Dtis("S"),
      FrameDependencyTemplate().Dtis("S").FrameDiffs({1}),
  };
  TestRtpSenderVideo rtp_sender_video = Create();
  rtp_sender_video.SetVideoStructure(&video_structure);

  // Send key frame.
  RTPVideoHeader hdr;
  RTPVideoHeader::GenericDescriptorInfo& generic = hdr.generic.emplace();
  generic.frame_id = kFrameId;
  generic.decode_target_indications = {DecodeTargetIndication::kSwitch};
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  ASSERT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {}));
  ASSERT_EQ(transport_.packets_sent(), 1);
  DependencyDescriptor descriptor_key;
  ASSERT_TRUE(transport_.last_sent_packet()
                  .GetExtension<RtpDependencyDescriptorExtension>(
                      nullptr, &descriptor_key));
  ASSERT_TRUE(descriptor_key.attached_structure);

  // Send delta frame that references the key frame 5275 frames back.
  generic.frame_id = kFrameId + 5275;
  generic.dependencies = {kFrameId};
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  EXPECT_FALSE(rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {}));
  EXPECT_EQ(transport_.packets_sent(), 1);

  // Send delta frame that references the dropped frame.
  generic.frame_id = kFrameId + 5276;
  generic.dependencies = {kFrameId + 5275};
  EXPECT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {}));
  ASSERT_EQ(transport_.packets_sent(), 2);
  DependencyDescriptor descriptor_delta;
  ASSERT_TRUE(
      transport_.last_sent_packet()
          .GetExtension<RtpDependencyDescriptorExtension>(
              descriptor_key.attached_structure.get(), &descriptor_delta));
  EXPECT_EQ(descriptor_delta.frame_number, (kFrameId + 5276) & 0xFFFF);
  EXPECT_THAT(descriptor_delta.frame_dependencies.frame_diffs, ElementsAre(1));
}

// The generic frame descriptor, even if negotiated, is not used instead of the
// dependency descriptor when the latter can't describe a delta frame.
TEST_F(RtpSenderVideoTest, DoesNotFallBackToGenericFrameDescriptor) {
  const int64_t kFrameId = 100000;
  uint8_t kFrame[100];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpDependencyDescriptorExtension::Uri(), kDependencyDescriptorId);
  rtp_module_->RegisterRtpHeaderExtension(
      RtpGenericFrameDescriptorExtension00::Uri(), kGenericDescriptorId);
  FrameDependencyStructure video_structure;
  video_structure.num_decode_targets = 1;
  video_structure.templates = {
      FrameDependencyTemplate().Dtis("S"),
      FrameDependencyTemplate().Dtis("S").FrameDiffs({1}),
  };
  TestRtpSenderVideo rtp_sender_video = Create();
  rtp_sender_video.SetVideoStructure(&video_structure);

  // Send key frame.
  RTPVideoHeader hdr;
  RTPVideoHeader::GenericDescriptorInfo& generic = hdr.generic.emplace();
  generic.frame_id = kFrameId;
  generic.decode_target_indications = {DecodeTargetIndication::kSwitch};
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  ASSERT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {}));
  ASSERT_EQ(transport_.packets_sent(), 1);
  ASSERT_TRUE(transport_.last_sent_packet()
                  .HasExtension<RtpDependencyDescriptorExtension>());

  // Send delta frame that references the key frame 5275 frames back, which is
  // too far back for the dependency descriptor, but not for the generic frame
  // descriptor.
  generic.frame_id = kFrameId + 5275;
  generic.dependencies = {kFrameId};
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  EXPECT_FALSE(rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {}));
  EXPECT_EQ(transport_.packets_sent(), 1);
}

TEST_F(RtpSenderVideoTest, PropagatesChainDiffsIntoDependencyDescriptor) {
  const int64_t kFrameId = 100000;
  uint8_t kFrame[100];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpDependencyDescriptorExtension::Uri(), kDependencyDescriptorId);
  FrameDependencyStructure video_structure;
  video_structure.num_decode_targets = 2;
  video_structure.num_chains = 1;
  video_structure.decode_target_protected_by_chain = {0, 0};
  video_structure.templates = {
      FrameDependencyTemplate().S(0).T(0).Dtis("SS").ChainDiffs({1}),
  };
  TestRtpSenderVideo rtp_sender_video = Create();
  rtp_sender_video.SetVideoStructure(&video_structure);

  RTPVideoHeader hdr;
  RTPVideoHeader::GenericDescriptorInfo& generic = hdr.generic.emplace();
  generic.frame_id = kFrameId;
  generic.decode_target_indications = {DecodeTargetIndication::kSwitch,
                                       DecodeTargetIndication::kSwitch};
  generic.chain_diffs = {2};
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  ASSERT_EQ(transport_.packets_sent(), 1);
  DependencyDescriptor descriptor_key;
  ASSERT_TRUE(transport_.last_sent_packet()
                  .GetExtension<RtpDependencyDescriptorExtension>(
                      nullptr, &descriptor_key));
  EXPECT_THAT(descriptor_key.frame_dependencies.chain_diffs,
              ContainerEq(generic.chain_diffs));
}

// A chain diff that the dependency descriptor can't represent, e.g. because
// the chain's previous frame was sent before a long pause, is sent as 0, i.e.
// the chain has no previous frame, rather than making the frame undescribable.
TEST_F(RtpSenderVideoTest, ReplacesUnrepresentableChainDiffWithZero) {
  const int64_t kFrameId = 100000;
  uint8_t kFrame[100];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpDependencyDescriptorExtension::Uri(), kDependencyDescriptorId);
  FrameDependencyStructure video_structure;
  video_structure.num_decode_targets = 1;
  video_structure.num_chains = 1;
  video_structure.decode_target_protected_by_chain = {0};
  video_structure.templates = {
      FrameDependencyTemplate().Dtis("S").ChainDiffs({0}),
      FrameDependencyTemplate().Dtis("S").FrameDiffs({1}).ChainDiffs({1}),
  };
  TestRtpSenderVideo rtp_sender_video = Create();
  rtp_sender_video.SetVideoStructure(&video_structure);

  // Send key frame.
  RTPVideoHeader hdr;
  RTPVideoHeader::GenericDescriptorInfo& generic = hdr.generic.emplace();
  generic.frame_id = kFrameId;
  generic.decode_target_indications = {DecodeTargetIndication::kSwitch};
  generic.chain_diffs = {0};
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  ASSERT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {}));
  ASSERT_EQ(transport_.packets_sent(), 1);
  DependencyDescriptor descriptor_key;
  ASSERT_TRUE(transport_.last_sent_packet()
                  .GetExtension<RtpDependencyDescriptorExtension>(
                      nullptr, &descriptor_key));
  ASSERT_TRUE(descriptor_key.attached_structure);

  // Send delta frame with a chain diff that doesn't fit in 8 bits.
  generic.frame_id = kFrameId + 1;
  generic.dependencies = {kFrameId};
  generic.chain_diffs = {300};
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  EXPECT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {}));
  ASSERT_EQ(transport_.packets_sent(), 2);
  DependencyDescriptor descriptor_delta;
  ASSERT_TRUE(
      transport_.last_sent_packet()
          .GetExtension<RtpDependencyDescriptorExtension>(
              descriptor_key.attached_structure.get(), &descriptor_delta));
  EXPECT_THAT(descriptor_delta.frame_dependencies.frame_diffs, ElementsAre(1));
  EXPECT_THAT(descriptor_delta.frame_dependencies.chain_diffs, ElementsAre(0));
}

TEST_F(RtpSenderVideoTest, SendsFrameWithLargeChainDiffOfInactiveChain) {
  const int64_t kFrameId = 100000;
  uint8_t kFrame[100];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpDependencyDescriptorExtension::Uri(), kDependencyDescriptorId);
  FrameDependencyStructure video_structure;
  video_structure.num_decode_targets = 2;
  video_structure.num_chains = 2;
  video_structure.decode_target_protected_by_chain = {0, 1};
  video_structure.templates = {
      FrameDependencyTemplate().Dtis("SS").ChainDiffs({0, 0}),
      FrameDependencyTemplate().Dtis("SS").FrameDiffs({1}).ChainDiffs({1, 1}),
  };
  TestRtpSenderVideo rtp_sender_video = Create();
  rtp_sender_video.SetVideoStructure(&video_structure);

  // Send key frame with only the first decode target, and thereby only the
  // first chain, active.
  RTPVideoHeader hdr;
  RTPVideoHeader::GenericDescriptorInfo& generic = hdr.generic.emplace();
  generic.frame_id = kFrameId;
  generic.decode_target_indications = {DecodeTargetIndication::kSwitch,
                                       DecodeTargetIndication::kSwitch};
  generic.active_decode_targets = 0b01;
  generic.chain_diffs = {0, 0};
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  ASSERT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {}));
  ASSERT_EQ(transport_.packets_sent(), 1);
  DependencyDescriptor descriptor_key;
  ASSERT_TRUE(transport_.last_sent_packet()
                  .GetExtension<RtpDependencyDescriptorExtension>(
                      nullptr, &descriptor_key));
  ASSERT_TRUE(descriptor_key.attached_structure);

  // Send delta frame with a chain diff that doesn't fit in 8 bits on the
  // inactive chain. That chain diff isn't serialized.
  generic.frame_id = kFrameId + 1;
  generic.dependencies = {kFrameId};
  generic.chain_diffs = {1, 300};
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  EXPECT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {}));
  ASSERT_EQ(transport_.packets_sent(), 2);
  DependencyDescriptor descriptor_delta;
  ASSERT_TRUE(
      transport_.last_sent_packet()
          .GetExtension<RtpDependencyDescriptorExtension>(
              descriptor_key.attached_structure.get(), &descriptor_delta));
  EXPECT_THAT(descriptor_delta.frame_dependencies.frame_diffs, ElementsAre(1));
  // The diff of the inactive chain has no meaning, so only the diff of the
  // active chain is checked.
  EXPECT_THAT(descriptor_delta.frame_dependencies.chain_diffs,
              ElementsAre(1, _));
}

TEST_F(RtpSenderVideoTest,
       PropagatesActiveDecodeTargetsIntoDependencyDescriptor) {
  const int64_t kFrameId = 100000;
  uint8_t kFrame[100];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpDependencyDescriptorExtension::Uri(), kDependencyDescriptorId);
  FrameDependencyStructure video_structure;
  video_structure.num_decode_targets = 2;
  video_structure.num_chains = 1;
  video_structure.decode_target_protected_by_chain = {0, 0};
  video_structure.templates = {
      FrameDependencyTemplate().S(0).T(0).Dtis("SS").ChainDiffs({1}),
  };
  TestRtpSenderVideo rtp_sender_video = Create();
  rtp_sender_video.SetVideoStructure(&video_structure);

  RTPVideoHeader hdr;
  RTPVideoHeader::GenericDescriptorInfo& generic = hdr.generic.emplace();
  generic.frame_id = kFrameId;
  generic.decode_target_indications = {DecodeTargetIndication::kSwitch,
                                       DecodeTargetIndication::kSwitch};
  generic.active_decode_targets = 0b01;
  generic.chain_diffs = {1};
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  ASSERT_EQ(transport_.packets_sent(), 1);
  DependencyDescriptor descriptor_key;
  ASSERT_TRUE(transport_.last_sent_packet()
                  .GetExtension<RtpDependencyDescriptorExtension>(
                      nullptr, &descriptor_key));
  EXPECT_EQ(descriptor_key.active_decode_targets_bitmask, 0b01u);
}

TEST_F(RtpSenderVideoTest,
       SetDiffentVideoStructureAvoidsCollisionWithThePreviousStructure) {
  const int64_t kFrameId = 100000;
  uint8_t kFrame[100];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpDependencyDescriptorExtension::Uri(), kDependencyDescriptorId);
  FrameDependencyStructure video_structure1;
  video_structure1.num_decode_targets = 2;
  video_structure1.templates = {
      FrameDependencyTemplate().S(0).T(0).Dtis("SS"),
      FrameDependencyTemplate().S(0).T(1).Dtis("D-"),
  };
  FrameDependencyStructure video_structure2;
  video_structure2.num_decode_targets = 2;
  video_structure2.templates = {
      FrameDependencyTemplate().S(0).T(0).Dtis("SS"),
      FrameDependencyTemplate().S(0).T(1).Dtis("R-"),
  };

  // Send 1st key frame.
  RTPVideoHeader hdr;
  RTPVideoHeader::GenericDescriptorInfo& generic = hdr.generic.emplace();
  generic.frame_id = kFrameId;
  generic.decode_target_indications = {DecodeTargetIndication::kSwitch,
                                       DecodeTargetIndication::kSwitch};
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  TestRtpSenderVideo rtp_sender_video = Create();
  rtp_sender_video.SetVideoStructure(&video_structure1);
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  // Parse 1st extension.
  ASSERT_EQ(transport_.packets_sent(), 1);
  DependencyDescriptor descriptor_key1;
  ASSERT_TRUE(transport_.last_sent_packet()
                  .GetExtension<RtpDependencyDescriptorExtension>(
                      nullptr, &descriptor_key1));
  ASSERT_TRUE(descriptor_key1.attached_structure);

  // Send the delta frame.
  generic.frame_id = kFrameId + 1;
  generic.temporal_index = 1;
  generic.decode_target_indications = {DecodeTargetIndication::kDiscardable,
                                       DecodeTargetIndication::kNotPresent};
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  ASSERT_EQ(transport_.packets_sent(), 2);
  RtpPacket delta_packet = transport_.last_sent_packet();

  // Send 2nd key frame.
  generic.frame_id = kFrameId + 2;
  generic.decode_target_indications = {DecodeTargetIndication::kSwitch,
                                       DecodeTargetIndication::kSwitch};
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  rtp_sender_video.SetVideoStructure(&video_structure2);
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  // Parse the 2nd key frame.
  ASSERT_EQ(transport_.packets_sent(), 3);
  DependencyDescriptor descriptor_key2;
  ASSERT_TRUE(transport_.last_sent_packet()
                  .GetExtension<RtpDependencyDescriptorExtension>(
                      nullptr, &descriptor_key2));
  ASSERT_TRUE(descriptor_key2.attached_structure);

  // Try to parse the 1st delta frame. It should parseble using the structure
  // from the 1st key frame, but not using the structure from the 2nd key frame.
  DependencyDescriptor descriptor_delta;
  EXPECT_TRUE(delta_packet.GetExtension<RtpDependencyDescriptorExtension>(
      descriptor_key1.attached_structure.get(), &descriptor_delta));
  EXPECT_FALSE(delta_packet.GetExtension<RtpDependencyDescriptorExtension>(
      descriptor_key2.attached_structure.get(), &descriptor_delta));
}

TEST_F(RtpSenderVideoTest,
       AuthenticateVideoHeaderWhenDependencyDescriptorExtensionIsUsed) {
  static constexpr size_t kFrameSize = 100;
  uint8_t kFrame[kFrameSize] = {1, 2, 3, 4};

  rtp_module_->RegisterRtpHeaderExtension(
      RtpDependencyDescriptorExtension::Uri(), kDependencyDescriptorId);
  auto encryptor = make_ref_counted<NiceMock<MockFrameEncryptor>>();
  ON_CALL(*encryptor, GetMaxCiphertextByteSize).WillByDefault(ReturnArg<1>());
  ON_CALL(*encryptor, Encrypt)
      .WillByDefault(WithArgs<3, 5>(
          [](std::span<const uint8_t> frame, size_t* bytes_written) {
            *bytes_written = frame.size();
            return 0;
          }));
  TestRtpSenderVideo rtp_sender_video =
      Create({.frame_encryptor = encryptor.get()});

  FrameDependencyStructure video_structure;
  video_structure.num_decode_targets = 1;
  video_structure.templates = {FrameDependencyTemplate().Dtis("S")};
  rtp_sender_video.SetVideoStructure(&video_structure);

  // Send key frame.
  RTPVideoHeader hdr;
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  hdr.generic.emplace().decode_target_indications =
      video_structure.templates[0].decode_target_indications;

  EXPECT_CALL(*encryptor,
              Encrypt(_, _, Not(IsEmpty()), ElementsAreArray(kFrame), _, _));
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  // Double check packet with the dependency descriptor is sent.
  ASSERT_EQ(transport_.packets_sent(), 1);
  EXPECT_TRUE(transport_.last_sent_packet()
                  .HasExtension<RtpDependencyDescriptorExtension>());
}

TEST_F(RtpSenderVideoTest, PopulateGenericFrameDescriptor) {
  const int64_t kFrameId = 100000;
  uint8_t kFrame[100];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpGenericFrameDescriptorExtension00::Uri(), kGenericDescriptorId);
  TestRtpSenderVideo rtp_sender_video = Create();

  RTPVideoHeader hdr;
  RTPVideoHeader::GenericDescriptorInfo& generic = hdr.generic.emplace();
  generic.frame_id = kFrameId;
  generic.temporal_index = 3;
  generic.spatial_index = 2;
  generic.dependencies.push_back(kFrameId - 1);
  generic.dependencies.push_back(kFrameId - 500);
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  RtpGenericFrameDescriptor descriptor_wire;
  EXPECT_EQ(1, transport_.packets_sent());
  ASSERT_TRUE(transport_.last_sent_packet()
                  .GetExtension<RtpGenericFrameDescriptorExtension00>(
                      &descriptor_wire));
  EXPECT_EQ(static_cast<uint16_t>(generic.frame_id), descriptor_wire.FrameId());
  EXPECT_EQ(generic.temporal_index, descriptor_wire.TemporalLayer());
  EXPECT_THAT(descriptor_wire.FrameDependenciesDiffs(), ElementsAre(1, 500));
  EXPECT_EQ(descriptor_wire.SpatialLayersBitmask(), 0b0000'0100);
}

// A dependency that the generic frame descriptor can't represent is left out
// rather than sent with a wrong frame diff.
TEST_F(RtpSenderVideoTest,
       LeavesOutDependencyThatGenericFrameDescriptorCannotRepresent) {
  const int64_t kFrameId = 100000;
  uint8_t kFrame[100];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpGenericFrameDescriptorExtension00::Uri(), kGenericDescriptorId);
  TestRtpSenderVideo rtp_sender_video = Create();

  // Send delta frame that references the previous frame and a frame with a
  // frame diff that is 5 when truncated to 16 bits.
  RTPVideoHeader hdr;
  RTPVideoHeader::GenericDescriptorInfo& generic = hdr.generic.emplace();
  generic.frame_id = kFrameId + (1 << 16) + 5;
  generic.dependencies = {kFrameId + (1 << 16) + 4, kFrameId};
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  EXPECT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {}));
  ASSERT_EQ(transport_.packets_sent(), 1);
  RtpGenericFrameDescriptor descriptor_wire;
  ASSERT_TRUE(transport_.last_sent_packet()
                  .GetExtension<RtpGenericFrameDescriptorExtension00>(
                      &descriptor_wire));
  EXPECT_THAT(descriptor_wire.FrameDependenciesDiffs(), ElementsAre(1));
}

TEST_F(RtpSenderVideoTest,
       UsesMinimalVp8DescriptorWhenGenericFrameDescriptorExtensionIsUsed) {
  const int64_t kFrameId = 100000;
  const size_t kFrameSize = 100;
  uint8_t kFrame[kFrameSize];

  rtp_module_->RegisterRtpHeaderExtension(
      RtpGenericFrameDescriptorExtension00::Uri(), kGenericDescriptorId);
  TestRtpSenderVideo rtp_sender_video = Create();

  RTPVideoHeader hdr;
  hdr.codec = kVideoCodecVP8;
  RTPVideoHeaderVP8& vp8 = hdr.video_type_header.emplace<RTPVideoHeaderVP8>();
  vp8.pictureId = kFrameId % 0X7FFF;
  vp8.tl0PicIdx = 13;
  vp8.temporalIdx = 1;
  vp8.keyIdx = 2;
  RTPVideoHeader::GenericDescriptorInfo& generic = hdr.generic.emplace();
  generic.frame_id = kFrameId;
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  rtp_sender_video.SendVideoFrame(kPayloadType, VideoCodecType::kVideoCodecVP8,
                                  kTimestampInfo, env_.clock().CurrentTime(),
                                  kFrame, sizeof(kFrame), hdr,
                                  kDefaultExpectedRetransmissionTime, {});

  ASSERT_EQ(transport_.packets_sent(), 1);
  // Expect only minimal 1-byte vp8 descriptor was generated.
  EXPECT_EQ(transport_.last_sent_packet().payload_size(), 1 + kFrameSize);
}

TEST_F(RtpSenderVideoTest, VideoLayersAllocationWithResolutionSentOnKeyFrames) {
  const size_t kFrameSize = 100;
  uint8_t kFrame[kFrameSize];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpVideoLayersAllocationExtension::Uri(),
      kVideoLayersAllocationExtensionId);
  TestRtpSenderVideo rtp_sender_video = Create();

  VideoLayersAllocation allocation;
  VideoLayersAllocation::SpatialLayer layer;
  layer.width = 360;
  layer.height = 180;
  layer.target_bitrate_per_temporal_layer.push_back(
      DataRate::KilobitsPerSec(50));
  allocation.resolution_and_frame_rate_is_valid = true;
  allocation.active_spatial_layers.push_back(layer);
  rtp_sender_video.SetVideoLayersAllocation(allocation);

  RTPVideoHeader hdr;
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  VideoLayersAllocation sent_allocation;
  EXPECT_TRUE(
      transport_.last_sent_packet()
          .GetExtension<RtpVideoLayersAllocationExtension>(&sent_allocation));
  EXPECT_THAT(sent_allocation.active_spatial_layers, ElementsAre(layer));

  // Next key frame also have the allocation.
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  EXPECT_TRUE(
      transport_.last_sent_packet()
          .GetExtension<RtpVideoLayersAllocationExtension>(&sent_allocation));
}

TEST_F(RtpSenderVideoTest,
       VideoLayersAllocationWithoutResolutionSentOnDeltaWhenUpdated) {
  const size_t kFrameSize = 100;
  uint8_t kFrame[kFrameSize];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpVideoLayersAllocationExtension::Uri(),
      kVideoLayersAllocationExtensionId);
  TestRtpSenderVideo rtp_sender_video = Create();

  VideoLayersAllocation allocation;
  VideoLayersAllocation::SpatialLayer layer;
  layer.width = 360;
  layer.height = 180;
  allocation.resolution_and_frame_rate_is_valid = true;
  layer.target_bitrate_per_temporal_layer.push_back(
      DataRate::KilobitsPerSec(50));
  allocation.active_spatial_layers.push_back(layer);
  rtp_sender_video.SetVideoLayersAllocation(allocation);

  RTPVideoHeader hdr;
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  EXPECT_TRUE(transport_.last_sent_packet()
                  .HasExtension<RtpVideoLayersAllocationExtension>());

  // No allocation sent on delta frame unless it has been updated.
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  EXPECT_FALSE(transport_.last_sent_packet()
                   .HasExtension<RtpVideoLayersAllocationExtension>());

  // Update the allocation.
  rtp_sender_video.SetVideoLayersAllocation(allocation);
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  VideoLayersAllocation sent_allocation;
  EXPECT_TRUE(
      transport_.last_sent_packet()
          .GetExtension<RtpVideoLayersAllocationExtension>(&sent_allocation));
  ASSERT_THAT(sent_allocation.active_spatial_layers, SizeIs(1));
  EXPECT_FALSE(sent_allocation.resolution_and_frame_rate_is_valid);
  EXPECT_THAT(sent_allocation.active_spatial_layers[0]
                  .target_bitrate_per_temporal_layer,
              SizeIs(1));
}

TEST_F(RtpSenderVideoTest,
       VideoLayersAllocationWithResolutionSentOnDeltaWhenSpatialLayerAdded) {
  const size_t kFrameSize = 100;
  uint8_t kFrame[kFrameSize];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpVideoLayersAllocationExtension::Uri(),
      kVideoLayersAllocationExtensionId);
  TestRtpSenderVideo rtp_sender_video = Create();

  VideoLayersAllocation allocation;
  allocation.resolution_and_frame_rate_is_valid = true;
  VideoLayersAllocation::SpatialLayer layer;
  layer.width = 360;
  layer.height = 180;
  layer.spatial_id = 0;
  layer.target_bitrate_per_temporal_layer.push_back(
      DataRate::KilobitsPerSec(50));
  allocation.active_spatial_layers.push_back(layer);
  rtp_sender_video.SetVideoLayersAllocation(allocation);

  RTPVideoHeader hdr;
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  ASSERT_TRUE(transport_.last_sent_packet()
                  .HasExtension<RtpVideoLayersAllocationExtension>());

  // Update the allocation.
  layer.width = 640;
  layer.height = 320;
  layer.spatial_id = 1;
  layer.target_bitrate_per_temporal_layer.push_back(
      DataRate::KilobitsPerSec(100));
  allocation.active_spatial_layers.push_back(layer);
  rtp_sender_video.SetVideoLayersAllocation(allocation);
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  VideoLayersAllocation sent_allocation;
  EXPECT_TRUE(
      transport_.last_sent_packet()
          .GetExtension<RtpVideoLayersAllocationExtension>(&sent_allocation));
  EXPECT_THAT(sent_allocation.active_spatial_layers, SizeIs(2));
  EXPECT_TRUE(sent_allocation.resolution_and_frame_rate_is_valid);
}

TEST_F(RtpSenderVideoTest,
       VideoLayersAllocationWithResolutionSentOnLargeFrameRateChange) {
  const size_t kFrameSize = 100;
  uint8_t kFrame[kFrameSize];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpVideoLayersAllocationExtension::Uri(),
      kVideoLayersAllocationExtensionId);
  TestRtpSenderVideo rtp_sender_video = Create();

  VideoLayersAllocation allocation;
  allocation.resolution_and_frame_rate_is_valid = true;
  VideoLayersAllocation::SpatialLayer layer;
  layer.width = 360;
  layer.height = 180;
  layer.spatial_id = 0;
  layer.frame_rate_fps = 10;
  layer.target_bitrate_per_temporal_layer.push_back(
      DataRate::KilobitsPerSec(50));
  allocation.active_spatial_layers.push_back(layer);
  rtp_sender_video.SetVideoLayersAllocation(allocation);

  RTPVideoHeader hdr;
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  ASSERT_TRUE(transport_.last_sent_packet()
                  .HasExtension<RtpVideoLayersAllocationExtension>());

  // Update frame rate only.
  allocation.active_spatial_layers[0].frame_rate_fps = 20;
  rtp_sender_video.SetVideoLayersAllocation(allocation);
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  VideoLayersAllocation sent_allocation;
  EXPECT_TRUE(
      transport_.last_sent_packet()
          .GetExtension<RtpVideoLayersAllocationExtension>(&sent_allocation));
  ASSERT_TRUE(sent_allocation.resolution_and_frame_rate_is_valid);
  EXPECT_EQ(sent_allocation.active_spatial_layers[0].frame_rate_fps, 20);
}

TEST_F(RtpSenderVideoTest,
       VideoLayersAllocationWithoutResolutionSentOnSmallFrameRateChange) {
  const size_t kFrameSize = 100;
  uint8_t kFrame[kFrameSize];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpVideoLayersAllocationExtension::Uri(),
      kVideoLayersAllocationExtensionId);
  TestRtpSenderVideo rtp_sender_video = Create();

  VideoLayersAllocation allocation;
  allocation.resolution_and_frame_rate_is_valid = true;
  VideoLayersAllocation::SpatialLayer layer;
  layer.width = 360;
  layer.height = 180;
  layer.spatial_id = 0;
  layer.frame_rate_fps = 10;
  layer.target_bitrate_per_temporal_layer.push_back(
      DataRate::KilobitsPerSec(50));
  allocation.active_spatial_layers.push_back(layer);
  rtp_sender_video.SetVideoLayersAllocation(allocation);

  RTPVideoHeader hdr;
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  ASSERT_TRUE(transport_.last_sent_packet()
                  .HasExtension<RtpVideoLayersAllocationExtension>());

  // Update frame rate slightly.
  allocation.active_spatial_layers[0].frame_rate_fps = 9;
  rtp_sender_video.SetVideoLayersAllocation(allocation);
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  VideoLayersAllocation sent_allocation;
  EXPECT_TRUE(
      transport_.last_sent_packet()
          .GetExtension<RtpVideoLayersAllocationExtension>(&sent_allocation));
  EXPECT_FALSE(sent_allocation.resolution_and_frame_rate_is_valid);
}

TEST_F(RtpSenderVideoTest, VideoLayersAllocationSentOnDeltaFramesOnlyOnUpdate) {
  const size_t kFrameSize = 100;
  uint8_t kFrame[kFrameSize];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpVideoLayersAllocationExtension::Uri(),
      kVideoLayersAllocationExtensionId);
  TestRtpSenderVideo rtp_sender_video = Create();

  VideoLayersAllocation allocation;
  VideoLayersAllocation::SpatialLayer layer;
  layer.width = 360;
  layer.height = 180;
  layer.target_bitrate_per_temporal_layer.push_back(
      DataRate::KilobitsPerSec(50));
  allocation.active_spatial_layers.push_back(layer);
  rtp_sender_video.SetVideoLayersAllocation(allocation);

  RTPVideoHeader hdr;
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  VideoLayersAllocation sent_allocation;
  EXPECT_TRUE(
      transport_.last_sent_packet()
          .GetExtension<RtpVideoLayersAllocationExtension>(&sent_allocation));
  EXPECT_THAT(sent_allocation.active_spatial_layers, SizeIs(1));

  // VideoLayersAllocation not sent on the next delta frame.
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  EXPECT_FALSE(transport_.last_sent_packet()
                   .HasExtension<RtpVideoLayersAllocationExtension>());

  // Update allocation. VideoLayesAllocation should be sent on the next frame.
  rtp_sender_video.SetVideoLayersAllocation(allocation);
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  EXPECT_TRUE(
      transport_.last_sent_packet()
          .GetExtension<RtpVideoLayersAllocationExtension>(&sent_allocation));
}

TEST_F(RtpSenderVideoTest, VideoLayersAllocationNotSentOnHigherTemporalLayers) {
  const size_t kFrameSize = 100;
  uint8_t kFrame[kFrameSize];
  rtp_module_->RegisterRtpHeaderExtension(
      RtpVideoLayersAllocationExtension::Uri(),
      kVideoLayersAllocationExtensionId);
  TestRtpSenderVideo rtp_sender_video = Create();

  VideoLayersAllocation allocation;
  allocation.resolution_and_frame_rate_is_valid = true;
  VideoLayersAllocation::SpatialLayer layer;
  layer.width = 360;
  layer.height = 180;
  layer.target_bitrate_per_temporal_layer.push_back(
      DataRate::KilobitsPerSec(50));
  allocation.active_spatial_layers.push_back(layer);
  rtp_sender_video.SetVideoLayersAllocation(allocation);

  RTPVideoHeader hdr;
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  hdr.codec = VideoCodecType::kVideoCodecVP8;
  auto& vp8_header = hdr.video_type_header.emplace<RTPVideoHeaderVP8>();
  vp8_header.temporalIdx = 1;

  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  EXPECT_FALSE(transport_.last_sent_packet()
                   .HasExtension<RtpVideoLayersAllocationExtension>());

  // Send a delta frame on tl0.
  vp8_header.temporalIdx = 0;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  EXPECT_TRUE(transport_.last_sent_packet()
                  .HasExtension<RtpVideoLayersAllocationExtension>());
}

TEST_F(RtpSenderVideoTest,
       AbsoluteCaptureTimeNotForwardedWhenImageHasNoCaptureTime) {
  uint8_t kFrame[kMaxPacketLength];
  rtp_module_->RegisterRtpHeaderExtension(AbsoluteCaptureTimeExtension::Uri(),
                                          kAbsoluteCaptureTimeExtensionId);
  TestRtpSenderVideo rtp_sender_video = Create();

  RTPVideoHeader hdr;
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  rtp_sender_video.SendVideoFrame(kPayloadType, kType, kTimestampInfo,
                                  /*capture_time=*/Timestamp::MinusInfinity(),
                                  kFrame, sizeof(kFrame), hdr,
                                  kDefaultExpectedRetransmissionTime, {});
  // No absolute capture time should be set as the capture_time_ms was the
  // default value.
  for (const RtpPacketReceived& packet : transport_.sent_packets()) {
    EXPECT_FALSE(packet.HasExtension<AbsoluteCaptureTimeExtension>());
  }
}

TEST_F(RtpSenderVideoTest, AbsoluteCaptureTime) {
  constexpr Timestamp kAbsoluteCaptureTimestamp = Timestamp::Millis(12345678);
  uint8_t kFrame[kMaxPacketLength];
  rtp_module_->RegisterRtpHeaderExtension(AbsoluteCaptureTimeExtension::Uri(),
                                          kAbsoluteCaptureTimeExtensionId);
  TestRtpSenderVideo rtp_sender_video = Create();

  RTPVideoHeader hdr;
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, kAbsoluteCaptureTimestamp, kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});

  std::optional<AbsoluteCaptureTime> absolute_capture_time;

  // It is expected that one and only one of the packets sent on this video
  // frame has absolute capture time header extension.
  for (const RtpPacketReceived& packet : transport_.sent_packets()) {
    if (absolute_capture_time.has_value()) {
      EXPECT_FALSE(packet.HasExtension<AbsoluteCaptureTimeExtension>());
    } else {
      absolute_capture_time =
          packet.GetExtension<AbsoluteCaptureTimeExtension>();
    }
  }

  // Verify the capture timestamp and that the clock offset is set to zero.
  ASSERT_TRUE(absolute_capture_time.has_value());
  EXPECT_EQ(
      absolute_capture_time->absolute_capture_timestamp,
      Int64MsToUQ32x32(env_.clock()
                           .ConvertTimestampToNtpTime(kAbsoluteCaptureTimestamp)
                           .ToMs()));
  EXPECT_EQ(absolute_capture_time->estimated_capture_clock_offset, 0);
}

TEST_F(RtpSenderVideoTest, AbsoluteCaptureTimeWithExtensionProvided) {
  constexpr AbsoluteCaptureTime kAbsoluteCaptureTime = {
      .absolute_capture_timestamp = 123,
      .estimated_capture_clock_offset = std::optional<int64_t>(456),
  };
  uint8_t kFrame[kMaxPacketLength];
  rtp_module_->RegisterRtpHeaderExtension(AbsoluteCaptureTimeExtension::Uri(),
                                          kAbsoluteCaptureTimeExtensionId);
  TestRtpSenderVideo rtp_sender_video = Create();

  RTPVideoHeader hdr;
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  hdr.absolute_capture_time = kAbsoluteCaptureTime;
  rtp_sender_video.SendVideoFrame(kPayloadType, kType, kTimestampInfo,
                                  /*capture_time=*/Timestamp::Millis(789),
                                  kFrame, sizeof(kFrame), hdr,
                                  kDefaultExpectedRetransmissionTime, {});

  std::optional<AbsoluteCaptureTime> absolute_capture_time;

  // It is expected that one and only one of the packets sent on this video
  // frame has absolute capture time header extension.
  for (const RtpPacketReceived& packet : transport_.sent_packets()) {
    if (absolute_capture_time.has_value()) {
      EXPECT_FALSE(packet.HasExtension<AbsoluteCaptureTimeExtension>());
    } else {
      absolute_capture_time =
          packet.GetExtension<AbsoluteCaptureTimeExtension>();
    }
  }

  // Verify the extension.
  EXPECT_EQ(absolute_capture_time, kAbsoluteCaptureTime);
}

TEST_F(RtpSenderVideoTest, PopulatesPlayoutDelay) {
  // Single packet frames.
  constexpr size_t kPacketSize = 123;
  uint8_t kFrame[kPacketSize];
  rtp_module_->RegisterRtpHeaderExtension(PlayoutDelayLimits::Uri(),
                                          kPlayoutDelayExtensionId);
  const VideoPlayoutDelay kExpectedDelay(TimeDelta::Millis(10),
                                         TimeDelta::Millis(20));
  TestRtpSenderVideo rtp_sender_video = Create();

  // Send initial key-frame without playout delay.
  RTPVideoHeader hdr;
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  hdr.codec = VideoCodecType::kVideoCodecVP8;
  auto& vp8_header = hdr.video_type_header.emplace<RTPVideoHeaderVP8>();
  vp8_header.temporalIdx = 0;

  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  EXPECT_FALSE(
      transport_.last_sent_packet().HasExtension<PlayoutDelayLimits>());

  // Set playout delay on a discardable frame.
  hdr.playout_delay = kExpectedDelay;
  hdr.frame_type = VideoFrameType::kVideoFrameDelta;
  vp8_header.temporalIdx = 1;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  VideoPlayoutDelay received_delay = VideoPlayoutDelay();
  ASSERT_TRUE(transport_.last_sent_packet().GetExtension<PlayoutDelayLimits>(
      &received_delay));
  EXPECT_EQ(received_delay, kExpectedDelay);

  // Set playout delay on a non-discardable frame, the extension should still
  // be populated since dilvery wasn't guaranteed on the last one.
  hdr.playout_delay = std::nullopt;  // Indicates "no change".
  vp8_header.temporalIdx = 0;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  ASSERT_TRUE(transport_.last_sent_packet().GetExtension<PlayoutDelayLimits>(
      &received_delay));
  EXPECT_EQ(received_delay, kExpectedDelay);

  // The next frame does not need the extensions since it's delivery has
  // already been guaranteed.
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  EXPECT_FALSE(
      transport_.last_sent_packet().HasExtension<PlayoutDelayLimits>());

  // Insert key-frame, we need to refresh the state here.
  hdr.frame_type = VideoFrameType::kVideoFrameKey;
  rtp_sender_video.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), kFrame,
      sizeof(kFrame), hdr, kDefaultExpectedRetransmissionTime, {});
  ASSERT_TRUE(transport_.last_sent_packet().GetExtension<PlayoutDelayLimits>(
      &received_delay));
  EXPECT_EQ(received_delay, kExpectedDelay);
}

TEST_F(RtpSenderVideoTest, SendGenericVideo) {
  const uint8_t kPayloadTypeGeneric = 127;
  const VideoCodecType kCodecType = VideoCodecType::kVideoCodecGeneric;
  const uint8_t kPayload[] = {47, 11, 32, 93, 89};
  TestRtpSenderVideo rtp_sender_video = Create();

  // Send keyframe.
  RTPVideoHeader video_header;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;
  ASSERT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadTypeGeneric, kCodecType, kTimestampInfo,
      env_.clock().CurrentTime(), kPayload, sizeof(kPayload), video_header,
      TimeDelta::PlusInfinity(), {}));

  std::span<const uint8_t> sent_payload =
      transport_.last_sent_packet().payload();
  uint8_t generic_header = sent_payload[0];
  EXPECT_TRUE(generic_header & RtpFormatVideoGeneric::kKeyFrameBit);
  EXPECT_TRUE(generic_header & RtpFormatVideoGeneric::kFirstPacketBit);
  EXPECT_THAT(sent_payload.subspan(1), ElementsAreArray(kPayload));

  // Send delta frame.
  const uint8_t kDeltaPayload[] = {13, 42, 32, 93, 13};
  video_header.frame_type = VideoFrameType::kVideoFrameDelta;
  ASSERT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadTypeGeneric, kCodecType, kTimestampInfo,
      env_.clock().CurrentTime(), kDeltaPayload, sizeof(kDeltaPayload),
      video_header, TimeDelta::PlusInfinity(), {}));

  sent_payload = sent_payload = transport_.last_sent_packet().payload();
  generic_header = sent_payload[0];
  EXPECT_FALSE(generic_header & RtpFormatVideoGeneric::kKeyFrameBit);
  EXPECT_TRUE(generic_header & RtpFormatVideoGeneric::kFirstPacketBit);
  EXPECT_THAT(sent_payload.subspan(1), ElementsAreArray(kDeltaPayload));
}

TEST_F(RtpSenderVideoTest, SendGenericVideoWithoutTimestampOffset) {
  const uint8_t kPayloadTypeGeneric = 127;
  const VideoCodecType kCodecType = VideoCodecType::kVideoCodecGeneric;
  const uint8_t kPayload[] = {47, 11, 32, 93, 89};
  TestRtpSenderVideo rtp_sender_video = Create();

  RTPVideoHeader video_header;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;

  ASSERT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadTypeGeneric, kCodecType,
      RtpTimestampInfo(RtpTimestampWithoutOffset{kRtpTimestamp}),
      env_.clock().CurrentTime(), kPayload, sizeof(kPayload), video_header,
      TimeDelta::PlusInfinity(), {}));

  EXPECT_EQ(transport_.last_sent_packet().Timestamp(),
            kRtpTimestamp + rtp_module_->RtpSender()->TimestampOffset());
}

TEST_F(RtpSenderVideoTest, SendRawVideo) {
  const uint8_t kPayloadTypeRaw = 111;
  const uint8_t kPayload[] = {11, 22, 33, 44, 55};
  TestRtpSenderVideo rtp_sender_video = Create({.raw_packetization = true});

  // Send a frame.
  RTPVideoHeader video_header;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;
  ASSERT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadTypeRaw, VideoCodecType::kVideoCodecGeneric, kTimestampInfo,
      env_.clock().CurrentTime(), kPayload, sizeof(kPayload), video_header,
      TimeDelta::PlusInfinity(), {}));

  std::span<const uint8_t> sent_payload =
      transport_.last_sent_packet().payload();
  EXPECT_THAT(sent_payload, ElementsAreArray(kPayload));
}

TEST_F(RtpSenderVideoTest, SendVideoFrameWithSetCodecTypeStillRaw) {
  const uint8_t kPayloadTypeRaw = 111;
  const uint8_t kPayload[] = {11, 22, 33, 44, 55};
  TestRtpSenderVideo rtp_sender_video = Create({.raw_packetization = true});

  // Send a frame with codectype "generic"
  RTPVideoHeader video_header;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;
  ASSERT_TRUE(rtp_sender_video.SendVideoFrame(
      kPayloadTypeRaw, VideoCodecType::kVideoCodecGeneric, kTimestampInfo,
      env_.clock().CurrentTime(), kPayload, sizeof(kPayload), video_header,
      TimeDelta::PlusInfinity(), {}));

  // Should still be packetized as raw.
  EXPECT_THAT(transport_.last_sent_packet().payload(),
              ElementsAreArray(kPayload));
}

std::unique_ptr<EncodedImage> CreateDefaultEncodedImage() {
  const uint8_t data[] = {1, 2, 3, 4};
  auto encoded_image = std::make_unique<EncodedImage>();
  encoded_image->SetEncodedData(EncodedImageBuffer::Create(data, sizeof(data)));
  return encoded_image;
}

TEST_F(RtpSenderVideoTest, CreateSenderRegistersFrameTransformer) {
  auto mock_frame_transformer =
      make_ref_counted<NiceMock<MockFrameTransformer>>();
  EXPECT_CALL(*mock_frame_transformer,
              RegisterTransformedFrameSinkCallback(_, kSsrc));
  TestRtpSenderVideo rtp_sender_video =
      Create({.transformer = mock_frame_transformer});
}

TEST_F(RtpSenderVideoTest, DestroySenderUnregistersFrameTransformer) {
  auto mock_frame_transformer =
      make_ref_counted<NiceMock<MockFrameTransformer>>();
  TestRtpSenderVideo rtp_sender_video =
      Create({.transformer = mock_frame_transformer});
  EXPECT_CALL(*mock_frame_transformer,
              UnregisterTransformedFrameSinkCallback(kSsrc));
}

TEST_F(RtpSenderVideoTest, SendEncodedImageTransformsFrame) {
  auto mock_frame_transformer =
      make_ref_counted<NiceMock<MockFrameTransformer>>();
  TestRtpSenderVideo rtp_sender_video =
      Create({.transformer = mock_frame_transformer});
  auto encoded_image = CreateDefaultEncodedImage();
  RTPVideoHeader video_header;

  EXPECT_CALL(*mock_frame_transformer, Transform);
  rtp_sender_video.SendEncodedImage(kPayloadType, kType, kRtpTimestamp,
                                    *encoded_image, video_header,
                                    kDefaultExpectedRetransmissionTime);
}

TEST_F(RtpSenderVideoTest, SendEncodedImageIncludesProvidedCsrcs) {
  TestRtpSenderVideo rtp_sender_video = Create();
  std::vector<uint32_t> expected_csrcs = {1, 2, 3};
  std::unique_ptr<EncodedImage> encoded_image = CreateDefaultEncodedImage();
  RTPVideoHeader video_header;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;

  ASSERT_TRUE(rtp_sender_video.SendEncodedImage(
      0, kType, kRtpTimestamp, *encoded_image, video_header,
      kDefaultExpectedRetransmissionTime, expected_csrcs));

  ASSERT_GT(transport_.packets_sent(), 0);
  std::vector<uint32_t> csrcs = transport_.last_sent_packet().Csrcs();
  EXPECT_EQ(csrcs, expected_csrcs);
}

// When Sframe is required but no Sframe encryptor is set up, the frame is
// dropped (fail closed) and nothing is put on the wire.
// Returns an Sframe encryptor that produces `ciphertext` for any frame.
scoped_refptr<NiceMock<MockSframeMediaEncryptor>> CreateSframeEncryptor(
    std::vector<uint8_t> ciphertext) {
  scoped_refptr<NiceMock<MockSframeMediaEncryptor>> encryptor =
      make_ref_counted<NiceMock<MockSframeMediaEncryptor>>();
  ON_CALL(*encryptor, GetMaxCiphertextByteSize)
      .WillByDefault(Return(ciphertext.size()));
  ON_CALL(*encryptor, Encrypt)
      .WillByDefault([ciphertext = std::move(ciphertext)](
                         std::span<const uint8_t> /*frame*/,
                         std::span<const uint8_t> /*additional_data*/,
                         std::span<uint8_t> encrypted_frame) {
        std::ranges::copy(ciphertext, encrypted_frame.begin());
        return RTCErrorOr<size_t>(ciphertext.size());
      });
  return encryptor;
}

TEST_F(RtpSenderVideoTest, SframeRequiredWithoutEncryptorDropsEncodedImage) {
  TestRtpSenderVideo sframe_sender =
      Create({.sframe_required_by_negotiation = true});
  std::unique_ptr<EncodedImage> encoded_image = CreateDefaultEncodedImage();
  RTPVideoHeader video_header;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;

  EXPECT_FALSE(sframe_sender.SendEncodedImage(
      kPayloadType, kType, kRtpTimestamp, *encoded_image, video_header,
      kDefaultExpectedRetransmissionTime, /*csrcs=*/{}));
  EXPECT_EQ(transport_.packets_sent(), 0);
}

// When Sframe is not required the frame is sent as usual.
TEST_F(RtpSenderVideoTest, SframeNotRequiredSendsEncodedImage) {
  TestRtpSenderVideo rtp_sender_video =
      Create({.sframe_required_by_negotiation = false});
  std::unique_ptr<EncodedImage> encoded_image = CreateDefaultEncodedImage();
  RTPVideoHeader video_header;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;

  EXPECT_TRUE(rtp_sender_video.SendEncodedImage(
      kPayloadType, kType, kRtpTimestamp, *encoded_image, video_header,
      kDefaultExpectedRetransmissionTime, /*csrcs=*/{}));
  EXPECT_GT(transport_.packets_sent(), 0);
}

// An encryptor may be configured before negotiation requires Sframe; frames
// are sent as plaintext until then.
TEST_F(RtpSenderVideoTest, SframeConfiguredButNotRequiredSendsPlaintext) {
  scoped_refptr<NiceMock<MockSframeMediaEncryptor>> encryptor =
      make_ref_counted<NiceMock<MockSframeMediaEncryptor>>();
  EXPECT_CALL(*encryptor, Encrypt).Times(0);
  TestRtpSenderVideo sender =
      Create({.sframe_required_by_negotiation = false,
              .sframe_encryption_config = SframeEncryptionConfig{
                  .mode = SframeMode::kPerFrame, .encryptor = encryptor}});
  std::unique_ptr<EncodedImage> encoded_image = CreateDefaultEncodedImage();
  RTPVideoHeader video_header;
  video_header.codec = kVideoCodecVP8;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;
  auto& vp8 = video_header.video_type_header.emplace<RTPVideoHeaderVP8>();
  vp8.InitRTPVideoHeaderVP8();

  ASSERT_TRUE(sender.SendEncodedImage(
      kPayloadType, kVideoCodecVP8, kRtpTimestamp, *encoded_image, video_header,
      kDefaultExpectedRetransmissionTime, {}));
  // VP8 payload descriptor with the S bit set, followed by the frame.
  EXPECT_THAT(transport_.last_sent_packet().payload(),
              ElementsAre(0x10, 1, 2, 3, 4));
}

TEST_F(RtpSenderVideoTest, SframeEncryptorWithoutKeyDropsEncodedImage) {
  TestRtpSenderVideo sframe_sender =
      Create({.sframe_required_by_negotiation = true,
              .sframe_encryption_config = SframeEncryptionConfig{
                  .mode = SframeMode::kPerFrame,
                  .encryptor = SframeEncryptor::Create(
                      SframeCipherSuite::kAes128GcmSha256_128)}});
  std::unique_ptr<EncodedImage> encoded_image = CreateDefaultEncodedImage();
  RTPVideoHeader video_header;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;

  EXPECT_FALSE(sframe_sender.SendEncodedImage(
      kPayloadType, kType, kRtpTimestamp, *encoded_image, video_header,
      kDefaultExpectedRetransmissionTime, /*csrcs=*/{}));
  EXPECT_EQ(transport_.packets_sent(), 0);
}

TEST_F(RtpSenderVideoTest, SframePerPacketDropsEncodedImage) {
  scoped_refptr<NiceMock<MockSframeMediaEncryptor>> encryptor =
      make_ref_counted<NiceMock<MockSframeMediaEncryptor>>();
  EXPECT_CALL(*encryptor, Encrypt).Times(0);
  TestRtpSenderVideo sender =
      Create({.sframe_required_by_negotiation = true,
              .sframe_encryption_config = SframeEncryptionConfig{
                  .mode = SframeMode::kPerPacket, .encryptor = encryptor}});
  std::unique_ptr<EncodedImage> encoded_image = CreateDefaultEncodedImage();
  RTPVideoHeader video_header;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;

  EXPECT_FALSE(sender.SendEncodedImage(
      kPayloadType, kType, kRtpTimestamp, *encoded_image, video_header,
      kDefaultExpectedRetransmissionTime, /*csrcs=*/{}));
  EXPECT_EQ(transport_.packets_sent(), 0);
}

// SendVideoFrame can be called without SendEncodedImage, e.g. after an encoded
// frame transform.
TEST_F(RtpSenderVideoTest, SframeWithoutSupportedEncryptorDropsVideoFrame) {
  TestRtpSenderVideo sender = Create({.sframe_required_by_negotiation = true});
  const std::vector<uint8_t> frame(100, 0x5a);
  RTPVideoHeader video_header;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;

  EXPECT_FALSE(sender.SendVideoFrame(
      kPayloadType, kType, kTimestampInfo, env_.clock().CurrentTime(), frame,
      frame.size(), video_header, kDefaultExpectedRetransmissionTime, {}));
  EXPECT_EQ(transport_.packets_sent(), 0);
}

// Sends a VP8 key frame with `frame` as its payload.
bool SendVp8KeyFrame(RTPSenderVideo& sender,
                     std::span<const uint8_t> frame,
                     bool is_last_frame_in_picture = true) {
  EncodedImage encoded_image;
  encoded_image.SetEncodedData(
      EncodedImageBuffer::Create(frame.data(), frame.size()));
  RTPVideoHeader video_header;
  video_header.codec = kVideoCodecVP8;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;
  video_header.is_last_frame_in_picture = is_last_frame_in_picture;
  auto& vp8 = video_header.video_type_header.emplace<RTPVideoHeaderVP8>();
  vp8.InitRTPVideoHeaderVP8();
  return sender.SendEncodedImage(kPayloadType, kVideoCodecVP8, kRtpTimestamp,
                                 encoded_image, video_header,
                                 kDefaultExpectedRetransmissionTime, {});
}

TEST_F(RtpSenderVideoTest, SframePerFrameSendsEncryptedFrame) {
  constexpr size_t kMaxRtpPacketSize = 1200;
  rtp_module_->RtpSender()->SetMaxRtpPacketSize(kMaxRtpPacketSize);
  const std::vector<uint8_t> frame = {1, 2, 3, 4};
  // Larger than a packet, so that it is split across packets.
  const std::vector<uint8_t> ciphertext(3000, 0xab);
  scoped_refptr<NiceMock<MockSframeMediaEncryptor>> encryptor =
      CreateSframeEncryptor(ciphertext);
  EXPECT_CALL(*encryptor, Encrypt(ElementsAreArray(frame), _, _));
  TestRtpSenderVideo sender =
      Create({.sframe_required_by_negotiation = true,
              .sframe_encryption_config = SframeEncryptionConfig{
                  .mode = SframeMode::kPerFrame, .encryptor = encryptor}});

  ASSERT_TRUE(SendVp8KeyFrame(sender, frame));

  ASSERT_GT(transport_.packets_sent(), 1);
  std::vector<uint8_t> sent_ciphertext;
  for (const RtpPacketReceived& packet : transport_.sent_packets()) {
    EXPECT_LE(packet.size(), kMaxRtpPacketSize);
    const std::span<const uint8_t> payload =
        packet.payload().subspan(SframeDescriptor::kSize);
    sent_ciphertext.insert(sent_ciphertext.end(), payload.begin(),
                           payload.end());
  }
  EXPECT_EQ(sent_ciphertext, ciphertext);
  EXPECT_TRUE(transport_.sent_packets().back().Marker());
}

// RED encapsulates the Sframe payload, i.e. the descriptor and the ciphertext.
TEST_F(RtpSenderVideoTest, SframePerFrameEncryptsBeforeRed) {
  const std::vector<uint8_t> ciphertext = {0xab, 0xcd};
  TestRtpSenderVideo sender =
      Create({.sframe_required_by_negotiation = true,
              .sframe_encryption_config =
                  SframeEncryptionConfig{
                      .mode = SframeMode::kPerFrame,
                      .encryptor = CreateSframeEncryptor(ciphertext)},
              .red_payload_type = kRedPayloadType});

  ASSERT_TRUE(SendVp8KeyFrame(sender, std::vector<uint8_t>{1, 2, 3, 4}));

  ASSERT_EQ(transport_.packets_sent(), 1);
  const RtpPacketReceived& packet = transport_.last_sent_packet();
  EXPECT_EQ(packet.PayloadType(), kRedPayloadType);
  // RED header with the media payload type, then the Sframe payload.
  ASSERT_EQ(packet.payload_size(),
            1 + SframeDescriptor::kSize + ciphertext.size());
  EXPECT_EQ(packet.payload()[0], kPayloadType);
  EXPECT_THAT(packet.payload().subspan(1 + SframeDescriptor::kSize),
              ElementsAreArray(ciphertext));
}

TEST_F(RtpSenderVideoTest, SframePerFrameDoesNotMarkFrameNotLastInPicture) {
  TestRtpSenderVideo sender =
      Create({.sframe_required_by_negotiation = true,
              .sframe_encryption_config = SframeEncryptionConfig{
                  .mode = SframeMode::kPerFrame,
                  .encryptor = CreateSframeEncryptor({0xab, 0xcd})}});

  ASSERT_TRUE(SendVp8KeyFrame(sender, std::vector<uint8_t>{1, 2, 3, 4},
                              /*is_last_frame_in_picture=*/false));

  EXPECT_FALSE(transport_.last_sent_packet().Marker());
}

TEST_F(RtpSenderVideoTest,
       SendEncodedImageWithTransformerIncludesProvidedCsrcs) {
  auto mock_frame_transformer =
      make_ref_counted<NiceMock<MockFrameTransformer>>();
  scoped_refptr<TransformedFrameCallback> callback;
  EXPECT_CALL(*mock_frame_transformer, RegisterTransformedFrameSinkCallback)
      .WillOnce(SaveArg<0>(&callback));
  TestRtpSenderVideo rtp_sender_video =
      Create({.transformer = mock_frame_transformer});
  ASSERT_TRUE(callback);
  ON_CALL(*mock_frame_transformer, Transform)
      .WillByDefault(
          [&callback](std::unique_ptr<TransformableFrameInterface> frame) {
            callback->OnTransformedFrame(std::move(frame));
          });

  auto encoded_image = CreateDefaultEncodedImage();
  std::vector<uint32_t> expected_csrcs = {1, 2, 3};
  RTPVideoHeader video_header;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;
  auto encoder_queue = time_controller_.GetTaskQueueFactory()->CreateTaskQueue(
      "encoder_queue", TaskQueueFactory::Priority::kNormal);
  encoder_queue->PostTask([&] {
    rtp_sender_video.SendEncodedImage(
        kPayloadType, kType, kRtpTimestamp, *encoded_image, video_header,
        kDefaultExpectedRetransmissionTime, expected_csrcs);
  });
  time_controller_.AdvanceTime(TimeDelta::Zero());

  ASSERT_GT(transport_.packets_sent(), 0);
  std::vector<uint32_t> csrcs = transport_.last_sent_packet().Csrcs();
  EXPECT_EQ(csrcs, expected_csrcs);
}

#if RTC_DCHECK_IS_ON && GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID)
TEST_F(RtpSenderVideoTest, ValidPayloadTypes) {
  auto mock_frame_transformer =
      make_ref_counted<NiceMock<MockFrameTransformer>>();
  TestRtpSenderVideo rtp_sender_video =
      Create({.transformer = mock_frame_transformer});
  auto encoded_image = CreateDefaultEncodedImage();
  RTPVideoHeader video_header;

  EXPECT_TRUE(rtp_sender_video.SendEncodedImage(
      0, kType, kRtpTimestamp, *encoded_image, video_header,
      kDefaultExpectedRetransmissionTime));
  EXPECT_TRUE(rtp_sender_video.SendEncodedImage(
      127, kType, kRtpTimestamp, *encoded_image, video_header,
      kDefaultExpectedRetransmissionTime));
  EXPECT_DEATH(rtp_sender_video.SendEncodedImage(
                   -1, kType, kRtpTimestamp, *encoded_image, video_header,
                   kDefaultExpectedRetransmissionTime),
               "");
  EXPECT_DEATH(rtp_sender_video.SendEncodedImage(
                   128, kType, kRtpTimestamp, *encoded_image, video_header,
                   kDefaultExpectedRetransmissionTime),
               "");
}
#endif

TEST_F(RtpSenderVideoTest, OnTransformedFrameSendsVideo) {
  auto mock_frame_transformer =
      make_ref_counted<NiceMock<MockFrameTransformer>>();
  scoped_refptr<TransformedFrameCallback> callback;
  EXPECT_CALL(*mock_frame_transformer, RegisterTransformedFrameSinkCallback)
      .WillOnce(SaveArg<0>(&callback));
  TestRtpSenderVideo rtp_sender_video =
      Create({.transformer = mock_frame_transformer});
  ASSERT_TRUE(callback);

  auto encoded_image = CreateDefaultEncodedImage();
  RTPVideoHeader video_header;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;
  ON_CALL(*mock_frame_transformer, Transform)
      .WillByDefault(
          [&callback](std::unique_ptr<TransformableFrameInterface> frame) {
            callback->OnTransformedFrame(std::move(frame));
          });
  auto encoder_queue = time_controller_.GetTaskQueueFactory()->CreateTaskQueue(
      "encoder_queue", TaskQueueFactory::Priority::kNormal);
  encoder_queue->PostTask([&] {
    rtp_sender_video.SendEncodedImage(kPayloadType, kType, kRtpTimestamp,
                                      *encoded_image, video_header,
                                      kDefaultExpectedRetransmissionTime);
  });
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_EQ(transport_.packets_sent(), 1);
  encoder_queue->PostTask([&] {
    rtp_sender_video.SendEncodedImage(kPayloadType, kType, kRtpTimestamp,
                                      *encoded_image, video_header,
                                      kDefaultExpectedRetransmissionTime);
  });
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_EQ(transport_.packets_sent(), 2);
}

TEST_F(RtpSenderVideoTest, TransformOverheadCorrectlyAccountedFor) {
  auto mock_frame_transformer =
      make_ref_counted<NiceMock<MockFrameTransformer>>();
  scoped_refptr<TransformedFrameCallback> callback;
  EXPECT_CALL(*mock_frame_transformer, RegisterTransformedFrameSinkCallback)
      .WillOnce(SaveArg<0>(&callback));
  TestRtpSenderVideo rtp_sender_video =
      Create({.transformer = mock_frame_transformer});
  ASSERT_TRUE(callback);

  auto encoded_image = CreateDefaultEncodedImage();
  RTPVideoHeader video_header;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;
  ON_CALL(*mock_frame_transformer, Transform)
      .WillByDefault(
          [&callback](std::unique_ptr<TransformableFrameInterface> frame) {
            const uint8_t data[] = {1, 2,  3,  4,  5,  6,  7,  8,
                                    9, 10, 11, 12, 13, 14, 15, 16};
            frame->SetData(data);
            callback->OnTransformedFrame(std::move(frame));
          });
  auto encoder_queue = time_controller_.GetTaskQueueFactory()->CreateTaskQueue(
      "encoder_queue", TaskQueueFactory::Priority::kNormal);
  const int kFramesPerSecond = 25;
  for (int i = 0; i < kFramesPerSecond; ++i) {
    encoder_queue->PostTask([&] {
      rtp_sender_video.SendEncodedImage(kPayloadType, kType, kRtpTimestamp,
                                        *encoded_image, video_header,
                                        kDefaultExpectedRetransmissionTime);
    });
    time_controller_.AdvanceTime(TimeDelta::Millis(1000 / kFramesPerSecond));
  }
  EXPECT_EQ(transport_.packets_sent(), kFramesPerSecond);
  EXPECT_GT(rtp_sender_video.PostEncodeOverhead().bps(), 2200);
}

TEST_F(RtpSenderVideoTest, TransformableFrameMetadataHasCorrectValue) {
  auto mock_frame_transformer =
      make_ref_counted<NiceMock<MockFrameTransformer>>();
  TestRtpSenderVideo rtp_sender_video = Create({
      .transformer = mock_frame_transformer,
  });
  auto encoded_image = CreateDefaultEncodedImage();
  RTPVideoHeader video_header;
  video_header.width = 1280u;
  video_header.height = 720u;
  RTPVideoHeader::GenericDescriptorInfo& generic =
      video_header.generic.emplace();
  generic.frame_id = 10;
  generic.temporal_index = 3;
  generic.spatial_index = 2;
  generic.decode_target_indications = {DecodeTargetIndication::kSwitch};
  generic.dependencies = {5};

  // Check that the transformable frame passed to the frame transformer has the
  // correct metadata.
  EXPECT_CALL(*mock_frame_transformer, Transform)
      .WillOnce(
          [](std::unique_ptr<TransformableFrameInterface> transformable_frame) {
            auto frame =
                absl::WrapUnique(static_cast<TransformableVideoFrameInterface*>(
                    transformable_frame.release()));
            ASSERT_TRUE(frame);
            auto metadata = frame->Metadata();
            EXPECT_EQ(frame->Rid(), "myrid");
            EXPECT_EQ(metadata.GetWidth(), 1280u);
            EXPECT_EQ(metadata.GetHeight(), 720u);
            EXPECT_EQ(metadata.GetFrameId(), 10);
            EXPECT_EQ(metadata.GetTemporalIndex(), 3);
            EXPECT_EQ(metadata.GetSpatialIndex(), 2);
            EXPECT_THAT(metadata.GetDependencies(), Optional(ElementsAre(5)));
            EXPECT_THAT(metadata.GetDecodeTargetIndications(),
                        ElementsAre(DecodeTargetIndication::kSwitch));
          });
  rtp_sender_video.SendEncodedImage(kPayloadType, kType, kRtpTimestamp,
                                    *encoded_image, video_header,
                                    kDefaultExpectedRetransmissionTime);
}

TEST_F(RtpSenderVideoTest, TransformableFrameHasCorrectCaptureIdentifier) {
  auto mock_frame_transformer =
      make_ref_counted<NiceMock<MockFrameTransformer>>();
  TestRtpSenderVideo rtp_sender_video =
      Create({.transformer = mock_frame_transformer});
  auto encoded_image = CreateDefaultEncodedImage();
  encoded_image->SetPresentationTimestamp(Timestamp::Millis(1));
  RTPVideoHeader video_header;

  EXPECT_CALL(*mock_frame_transformer, Transform)
      .WillOnce([&encoded_image](std::unique_ptr<TransformableFrameInterface>
                                     transformable_frame) {
        auto* frame = static_cast<TransformableVideoFrameInterface*>(
            transformable_frame.get());
        ASSERT_TRUE(frame);
        EXPECT_EQ(frame->GetPresentationTimestamp(),
                  encoded_image->PresentationTimestamp());
      });
  rtp_sender_video.SendEncodedImage(kPayloadType, kType, kRtpTimestamp,
                                    *encoded_image, video_header,
                                    kDefaultExpectedRetransmissionTime);
}

TEST_F(RtpSenderVideoTest, OnTransformedFrameSendsVideoWhenCloned) {
  auto mock_frame_transformer =
      make_ref_counted<NiceMock<MockFrameTransformer>>();
  scoped_refptr<TransformedFrameCallback> callback;
  EXPECT_CALL(*mock_frame_transformer, RegisterTransformedFrameSinkCallback)
      .WillOnce(SaveArg<0>(&callback));
  TestRtpSenderVideo rtp_sender_video =
      Create({.transformer = mock_frame_transformer});
  ASSERT_TRUE(callback);

  auto encoded_image = CreateDefaultEncodedImage();
  RTPVideoHeader video_header;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;
  ON_CALL(*mock_frame_transformer, Transform)
      .WillByDefault(
          [&callback](std::unique_ptr<TransformableFrameInterface> frame) {
            auto clone = CloneVideoFrame(
                static_cast<TransformableVideoFrameInterface*>(frame.get()));
            EXPECT_TRUE(clone);
            callback->OnTransformedFrame(std::move(clone));
          });
  auto encoder_queue = time_controller_.GetTaskQueueFactory()->CreateTaskQueue(
      "encoder_queue", TaskQueueFactory::Priority::kNormal);
  encoder_queue->PostTask([&] {
    rtp_sender_video.SendEncodedImage(kPayloadType, kType, kRtpTimestamp,
                                      *encoded_image, video_header,
                                      kDefaultExpectedRetransmissionTime);
  });
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_EQ(transport_.packets_sent(), 1);
  encoder_queue->PostTask([&] {
    rtp_sender_video.SendEncodedImage(kPayloadType, kType, kRtpTimestamp,
                                      *encoded_image, video_header,
                                      kDefaultExpectedRetransmissionTime);
  });
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_EQ(transport_.packets_sent(), 2);
}

}  // namespace
}  // namespace webrtc
