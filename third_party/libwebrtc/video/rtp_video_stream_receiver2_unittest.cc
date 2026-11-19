/*
 *  Copyright 2017 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/rtp_video_stream_receiver2.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/frame_transformer_interface.h"
#include "api/make_ref_counted.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_headers.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/test/mock_frame_transformer.h"
#include "api/transport/rtp/corruption_detection_message.h"
#include "api/transport/rtp/dependency_descriptor.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/color_space.h"
#include "api/video/corruption_detection/frame_instrumentation_data.h"
#include "api/video/encoded_frame.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_timing.h"
#include "call/rtp_packet_sink_interface.h"
#include "call/test/mock_rtp_packet_sink_interface.h"
#include "call/video_receive_stream.h"
#include "common_video/h264/h264_common.h"
#include "logging/rtc_event_log/mock/mock_rtc_event_log.h"
#include "media/base/media_constants.h"
#include "modules/include/module_common_types.h"
#include "modules/rtp_rtcp/include/rtp_header_extension_map.h"
#include "modules/rtp_rtcp/source/corruption_detection_extension.h"
#include "modules/rtp_rtcp/source/rtp_dependency_descriptor_extension.h"
#include "modules/rtp_rtcp/source/rtp_format.h"
#include "modules/rtp_rtcp/source/rtp_format_vp9.h"
#include "modules/rtp_rtcp/source/rtp_generic_frame_descriptor.h"
#include "modules/rtp_rtcp/source/rtp_generic_frame_descriptor_extension.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "modules/rtp_rtcp/source/rtp_video_header.h"
#include "modules/video_coding/codecs/h264/include/h264_globals.h"
#include "modules/video_coding/codecs/vp8/include/vp8_globals.h"
#include "modules/video_coding/codecs/vp9/include/vp9_globals.h"
#include "modules/video_coding/nack_requester.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "system_wrappers/include/ntp_time.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/mock_transport.h"
#include "test/rtcp_packet_parser.h"
#include "test/time_controller/simulated_time_controller.h"
#include "test/time_controller/simulated_time_controller_impl.h"

namespace webrtc {

namespace {

using ::testing::_;
using ::testing::DoubleNear;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;
using ::testing::Values;

constexpr uint8_t kH264StartCode[] = {0x00, 0x00, 0x00, 0x01};

// Corruption detection metrics for testing.
constexpr double kStd = 1.0;
constexpr int kLumaThreshold = 5;
constexpr int kChormaThreshold = 3;
constexpr int kVp9PayloadType = 99;
constexpr int kNumSamples = 13;
// 8 bits.
constexpr int kMaxSequenceIdx = 127;

std::vector<uint64_t> GetAbsoluteCaptureTimestamps(const EncodedFrame* frame) {
  std::vector<uint64_t> result;
  for (const auto& packet_info : frame->PacketInfos()) {
    if (packet_info.absolute_capture_time()) {
      result.push_back(
          packet_info.absolute_capture_time()->absolute_capture_timestamp);
    }
  }
  return result;
}

RTPVideoHeader GetGenericVideoHeader(VideoFrameType frame_type) {
  RTPVideoHeader video_header;
  video_header.is_first_packet_in_frame = true;
  video_header.is_last_packet_in_frame = true;
  video_header.codec = kVideoCodecGeneric;
  video_header.frame_type = frame_type;
  return video_header;
}

class MockNackSender : public NackSender {
 public:
  MOCK_METHOD(void,
              SendNack,
              (const std::vector<uint16_t>& sequence_numbers,
               bool buffering_allowed),
              (override));
};

class MockKeyFrameRequestSender : public KeyFrameRequestSender {
 public:
  MOCK_METHOD(void, RequestKeyFrame, (), (override));
};

class MockOnCompleteFrameCallback
    : public RtpVideoStreamReceiver2::OnCompleteFrameCallback {
 public:
  MOCK_METHOD(void, DoOnCompleteFrame, (EncodedFrame*), ());
  MOCK_METHOD(void, DoOnCompleteFrameFailNullptr, (EncodedFrame*), ());
  MOCK_METHOD(void, DoOnCompleteFrameFailLength, (EncodedFrame*), ());
  MOCK_METHOD(void, DoOnCompleteFrameFailBitstream, (EncodedFrame*), ());
  void OnCompleteFrame(std::unique_ptr<EncodedFrame> frame) override {
    if (frame == nullptr) {
      DoOnCompleteFrameFailNullptr(nullptr);
      return;
    }
    EXPECT_EQ(buffer_.size(), frame->size());
    if (buffer_.size() != frame->size()) {
      DoOnCompleteFrameFailLength(frame.get());
      return;
    }
    if (memcmp(buffer_.data(), frame->data(), buffer_.size()) != 0) {
      DoOnCompleteFrameFailBitstream(frame.get());
      return;
    }
    DoOnCompleteFrame(frame.get());
  }

  void ClearExpectedBitstream() { buffer_.clear(); }

  void AppendExpectedBitstream(std::span<const uint8_t> data) {
    buffer_.insert(buffer_.end(), data.begin(), data.end());
  }
  std::vector<uint8_t> buffer_;
};

constexpr uint32_t kSsrc = 111;
constexpr int kPayloadType = 100;
constexpr int kRedPayloadType = 125;
constexpr TimeDelta kMaxWaitForKeyframe = TimeDelta::Millis(200);

std::unique_ptr<RtpPacketReceived> CreateRtpPacketReceived() {
  constexpr uint16_t kSequenceNumber = 222;
  auto packet = std::make_unique<RtpPacketReceived>();
  packet->SetSsrc(kSsrc);
  packet->SetSequenceNumber(kSequenceNumber);
  packet->SetPayloadType(kPayloadType);
  return packet;
}

MATCHER_P(SamePacketAs, other, "") {
  return arg.Ssrc() == other.Ssrc() &&
         arg.SequenceNumber() == other.SequenceNumber();
}

}  // namespace

class RtpVideoStreamReceiver2Test : public ::testing::Test,
                                    public RtpPacketSinkInterface {
 public:
  RtpVideoStreamReceiver2Test() : RtpVideoStreamReceiver2Test("") {}
  explicit RtpVideoStreamReceiver2Test(std::string field_trials)
      : time_controller_(Timestamp::Millis(100)),
        env_(CreateEnvironment(CreateTestFieldTrialsPtr(field_trials),
                               time_controller_.GetClock(),
                               time_controller_.GetTaskQueueFactory(),
                               &log_)),
        task_queue_(time_controller_.GetTaskQueueFactory()->CreateTaskQueue(
            "RtpVideoStreamReceiver2Test",
            TaskQueueFactory::Priority::kNormal)),
        task_queue_setter_(task_queue_.get()),
        config_(CreateConfig()) {
    rtp_receive_statistics_ = ReceiveStatistics::Create(&env_.clock());
    rtp_video_stream_receiver_ = std::make_unique<RtpVideoStreamReceiver2>(
        env_, TaskQueueBase::Current(), &mock_transport_, nullptr, nullptr,
        &config_, kMaxWaitForKeyframe, rtp_receive_statistics_.get(), nullptr,
        nullptr, &nack_periodic_processor_, &mock_on_complete_frame_callback_,
        nullptr, nullptr, nullptr);
    rtp_video_stream_receiver_->AddReceiveCodec(kPayloadType,
                                                kVideoCodecGeneric, {},
                                                /*raw_payload=*/false);
    ON_CALL(mock_transport_, SendRtcp)
        .WillByDefault(
            [this](std::span<const uint8_t> packet, ::testing::Unused) {
              return rtcp_packet_parser_.Parse(packet);
            });
  }

  RTPVideoHeader GetDefaultH264VideoHeader() {
    RTPVideoHeader video_header;
    video_header.codec = kVideoCodecH264;
    video_header.video_type_header.emplace<RTPVideoHeaderH264>();
    return video_header;
  }

  // TODO(Johan): refactor h264_sps_pps_tracker_unittests.cc to avoid duplicate
  // code.
  void AddSps(RTPVideoHeader* video_header,
              uint8_t sps_id,
              CopyOnWriteBuffer* data) {
    NaluInfo info;
    info.type = H264::NaluType::kSps;
    info.sps_id = sps_id;
    info.pps_id = -1;
    data->AppendData<uint8_t, 2>({H264::NaluType::kSps, sps_id});
    auto& h264 = std::get<RTPVideoHeaderH264>(video_header->video_type_header);
    h264.nalus.push_back(info);
  }

  void AddPps(RTPVideoHeader* video_header,
              uint8_t sps_id,
              uint8_t pps_id,
              CopyOnWriteBuffer* data) {
    NaluInfo info;
    info.type = H264::NaluType::kPps;
    info.sps_id = sps_id;
    info.pps_id = pps_id;
    data->AppendData<uint8_t, 2>({H264::NaluType::kPps, pps_id});
    auto& h264 = std::get<RTPVideoHeaderH264>(video_header->video_type_header);
    h264.nalus.push_back(info);
  }

  void AddIdr(RTPVideoHeader* video_header, int pps_id) {
    NaluInfo info;
    info.type = H264::NaluType::kIdr;
    info.sps_id = -1;
    info.pps_id = pps_id;
    auto& h264 = std::get<RTPVideoHeaderH264>(video_header->video_type_header);
    h264.nalus.push_back(info);
  }

  void OnRtpPacket(const RtpPacketReceived& packet) override {
    if (test_packet_sink_)
      test_packet_sink_->OnRtpPacket(packet);
  }

 protected:
  VideoReceiveStreamInterface::Config CreateConfig() {
    VideoReceiveStreamInterface::Config config(nullptr);
    config.rtp.remote_ssrc = 1111;
    config.rtp.red_payload_type = kRedPayloadType;
    config.rtp.packet_sink_ = this;
    return config;
  }

  GlobalSimulatedTimeController time_controller_;
  MockRtcEventLog log_;
  Environment env_;
  std::unique_ptr<TaskQueueBase, TaskQueueDeleter> task_queue_;
  TokenTaskQueue::CurrentTaskQueueSetter task_queue_setter_;

  VideoReceiveStreamInterface::Config config_;
  NackPeriodicProcessor nack_periodic_processor_;
  test::RtcpPacketParser rtcp_packet_parser_;
  MockTransport mock_transport_;
  MockOnCompleteFrameCallback mock_on_complete_frame_callback_;
  std::unique_ptr<ReceiveStatistics> rtp_receive_statistics_;
  std::unique_ptr<RtpVideoStreamReceiver2> rtp_video_stream_receiver_;
  RtpPacketSinkInterface* test_packet_sink_ = nullptr;
};

TEST_F(RtpVideoStreamReceiver2Test, CacheColorSpaceFromLastPacketOfKeyframe) {
  // Test that color space is cached from the last packet of a key frame and
  // that it's not reset by padding packets without color space.
  const ColorSpace kColorSpace(
      ColorSpace::PrimaryID::kFILM, ColorSpace::TransferID::kBT2020_12,
      ColorSpace::MatrixID::kBT2020_NCL, ColorSpace::RangeID::kFull);
  const std::vector<uint8_t> kKeyFramePayload = {0, 1, 2, 3, 4, 5,
                                                 6, 7, 8, 9, 10};
  const std::vector<uint8_t> kDeltaFramePayload = {0, 1, 2, 3, 4};

  // Anonymous helper class that generates received packets.
  class {
   public:
    void SetPayload(const std::vector<uint8_t>& payload,
                    VideoFrameType video_frame_type) {
      video_frame_type_ = video_frame_type;
      RtpPacketizer::PayloadSizeLimits pay_load_size_limits;
      // Reduce max payload length to make sure the key frame generates two
      // packets.
      pay_load_size_limits.max_payload_len = 8;
      RTPVideoHeaderVP9 rtp_video_header_vp9;
      rtp_video_header_vp9.InitRTPVideoHeaderVP9();
      rtp_video_header_vp9.inter_pic_predicted =
          (video_frame_type == VideoFrameType::kVideoFrameDelta);
      rtp_packetizer_ = std::make_unique<RtpPacketizerVp9>(
          payload, pay_load_size_limits, rtp_video_header_vp9);
    }

    size_t NumPackets() { return rtp_packetizer_->NumPackets(); }
    void SetColorSpace(const ColorSpace& color_space) {
      color_space_ = color_space;
    }

    RtpPacketReceived NextPacket() {
      RtpHeaderExtensionMap extension_map;
      extension_map.Register<ColorSpaceExtension>(RtpHeaderExtensionId(1));
      RtpPacketToSend packet_to_send(&extension_map);
      packet_to_send.SetSequenceNumber(sequence_number_++);
      packet_to_send.SetSsrc(kSsrc);
      packet_to_send.SetPayloadType(kVp9PayloadType);
      bool include_color_space =
          (rtp_packetizer_->NumPackets() == 1u &&
           video_frame_type_ == VideoFrameType::kVideoFrameKey);
      if (include_color_space) {
        EXPECT_TRUE(
            packet_to_send.SetExtension<ColorSpaceExtension>(color_space_));
      }
      rtp_packetizer_->NextPacket(&packet_to_send);

      RtpPacketReceived received_packet(&extension_map);
      received_packet.Parse(packet_to_send.data(), packet_to_send.size());
      return received_packet;
    }

   private:
    uint16_t sequence_number_ = 0;
    VideoFrameType video_frame_type_;
    ColorSpace color_space_;
    std::unique_ptr<RtpPacketizer> rtp_packetizer_;
  } received_packet_generator;
  received_packet_generator.SetColorSpace(kColorSpace);

  // Prepare the receiver for VP9.
  CodecParameterMap codec_params;
  rtp_video_stream_receiver_->AddReceiveCodec(kVp9PayloadType, kVideoCodecVP9,
                                              codec_params,
                                              /*raw_payload=*/false);

  // Generate key frame packets.
  received_packet_generator.SetPayload(kKeyFramePayload,
                                       VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(received_packet_generator.NumPackets(), 2u);
  RtpPacketReceived key_frame_packet1 = received_packet_generator.NextPacket();
  RtpPacketReceived key_frame_packet2 = received_packet_generator.NextPacket();

  // Generate delta frame packet.
  received_packet_generator.SetPayload(kDeltaFramePayload,
                                       VideoFrameType::kVideoFrameDelta);
  EXPECT_EQ(received_packet_generator.NumPackets(), 1u);
  RtpPacketReceived delta_frame_packet = received_packet_generator.NextPacket();

  rtp_video_stream_receiver_->StartReceive();
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kKeyFramePayload);

  // Send the key frame and expect a callback with color space information.
  EXPECT_FALSE(key_frame_packet1.GetExtension<ColorSpaceExtension>());
  EXPECT_TRUE(key_frame_packet2.GetExtension<ColorSpaceExtension>());
  rtp_video_stream_receiver_->OnRtpPacket(key_frame_packet1);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_))
      .WillOnce([kColorSpace](EncodedFrame* frame) {
        ASSERT_TRUE(frame->EncodedImage().ColorSpace());
        EXPECT_EQ(*frame->EncodedImage().ColorSpace(), kColorSpace);
      });
  rtp_video_stream_receiver_->OnRtpPacket(key_frame_packet2);
  // Resend the first key frame packet to simulate padding for example.
  rtp_video_stream_receiver_->OnRtpPacket(key_frame_packet1);

  mock_on_complete_frame_callback_.ClearExpectedBitstream();
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kDeltaFramePayload);

  // Expect delta frame to have color space set even though color space not
  // included in the RTP packet.
  EXPECT_FALSE(delta_frame_packet.GetExtension<ColorSpaceExtension>());
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_))
      .WillOnce([kColorSpace](EncodedFrame* frame) {
        ASSERT_TRUE(frame->EncodedImage().ColorSpace());
        EXPECT_EQ(*frame->EncodedImage().ColorSpace(), kColorSpace);
      });
  rtp_video_stream_receiver_->OnRtpPacket(delta_frame_packet);
}

class ReceivedPacketGenerator {
 public:
  ReceivedPacketGenerator() = default;

  void SetPayload(const std::vector<uint8_t>& payload,
                  VideoFrameType video_frame_type) {
    video_frame_type_ = video_frame_type;
    RtpPacketizer::PayloadSizeLimits pay_load_size_limits;
    RTPVideoHeaderVP9 rtp_video_header_vp9;
    rtp_video_header_vp9.InitRTPVideoHeaderVP9();
    rtp_video_header_vp9.inter_pic_predicted =
        (video_frame_type == VideoFrameType::kVideoFrameDelta);
    rtp_packetizer_ = std::make_unique<RtpPacketizerVp9>(
        payload, pay_load_size_limits, rtp_video_header_vp9);
  }

  size_t NumPackets() { return rtp_packetizer_->NumPackets(); }

  void SetCorruptionDetectionHeader(const CorruptionDetectionMessage& msg) {
    corruption_detection_msg_ = msg;
  }

  RtpPacketReceived NextPacket(bool include_corruption_header) {
    RtpHeaderExtensionMap extension_map;
    extension_map.Register<CorruptionDetectionExtension>(
        RtpHeaderExtensionId(/*id=*/1));
    RtpPacketToSend packet_to_send(&extension_map);
    packet_to_send.SetSequenceNumber(sequence_number_++);
    packet_to_send.SetSsrc(kSsrc);
    packet_to_send.SetPayloadType(kVp9PayloadType);
    packet_to_send.SetTimestamp(timestamp_++);
    if (include_corruption_header) {
      EXPECT_TRUE(packet_to_send.SetExtension<CorruptionDetectionExtension>(
          corruption_detection_msg_));
    }
    rtp_packetizer_->NextPacket(&packet_to_send);

    RtpPacketReceived received_packet(&extension_map);
    received_packet.Parse(packet_to_send.data(), packet_to_send.size());
    return received_packet;
  }

 private:
  uint16_t sequence_number_ = 0;
  uint32_t timestamp_ = 0;
  VideoFrameType video_frame_type_;
  CorruptionDetectionMessage corruption_detection_msg_;
  std::unique_ptr<RtpPacketizer> rtp_packetizer_;
};

std::optional<CorruptionDetectionMessage> GetCorruptionDetectionMessage(
    int sequence_idx,
    bool interpret_as_MSB) {
  CorruptionDetectionMessage::Builder builder;
  builder.WithSequenceIndex(sequence_idx);
  builder.WithInterpretSequenceIndexAsMostSignificantBits(interpret_as_MSB);
  builder.WithStdDev(kStd);
  builder.WithLumaErrorThreshold(kLumaThreshold);
  builder.WithChromaErrorThreshold(kChormaThreshold);

  double sample_value = 0.5;
  std::vector<double> sample_values;
  for (int i = 0; i < kNumSamples; i++) {
    sample_values.push_back(sample_value);
    sample_value += 0.5;
  }
  builder.WithSampleValues(sample_values);

  std::optional<CorruptionDetectionMessage> kCorruptionDetectionMsg =
      builder.Build();
  return kCorruptionDetectionMsg;
}

TEST_F(RtpVideoStreamReceiver2Test,
       FrameInstrumentationDataGetsPopulatedLSBIncreasedCorrectly) {
  const std::vector<uint8_t> kKeyFramePayload = {0, 1, 2, 3, 4};
  const std::vector<uint8_t> kDeltaFramePayload = {5, 6, 7, 8, 9};

  // Prepare the receiver for VP9.
  CodecParameterMap codec_params;
  rtp_video_stream_receiver_->AddReceiveCodec(kVp9PayloadType, kVideoCodecVP9,
                                              codec_params,
                                              /*raw_payload=*/false);

  ReceivedPacketGenerator received_packet_generator;
  std::optional<CorruptionDetectionMessage> corruption_detection_msg =
      GetCorruptionDetectionMessage(
          /*sequence_idx=*/0, /*interpret_as_MSB*/ true);
  ASSERT_TRUE(corruption_detection_msg.has_value());
  received_packet_generator.SetCorruptionDetectionHeader(
      *corruption_detection_msg);

  // Generate key frame packets.
  received_packet_generator.SetPayload(kKeyFramePayload,
                                       VideoFrameType::kVideoFrameKey);
  // Have corruption header on the key frame.
  RtpPacketReceived key_frame_packet =
      received_packet_generator.NextPacket(/*include_corruption_header=*/true);

  // Generate delta frame packet.
  received_packet_generator.SetPayload(kDeltaFramePayload,
                                       VideoFrameType::kVideoFrameDelta);

  // Second message sets the LSB instead of MSB, increasing the counter by the
  // number of samples in the first message.
  corruption_detection_msg = GetCorruptionDetectionMessage(
      /*sequence_idx=*/kNumSamples, /*interpret_as_MSB*/ false);
  ASSERT_TRUE(corruption_detection_msg.has_value());
  received_packet_generator.SetCorruptionDetectionHeader(
      *corruption_detection_msg);
  RtpPacketReceived delta_frame_packet =
      received_packet_generator.NextPacket(/*include_corruption_header=*/true);

  rtp_video_stream_receiver_->StartReceive();
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kKeyFramePayload);

  EXPECT_TRUE(key_frame_packet.GetExtension<CorruptionDetectionExtension>());
  std::unique_ptr<EncodedFrame> key_encoded_frame;
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_))
      .WillOnce([&](EncodedFrame* encoded_frame) {
        key_encoded_frame = std::make_unique<EncodedFrame>(*encoded_frame);
      });
  rtp_video_stream_receiver_->OnRtpPacket(key_frame_packet);
  ASSERT_TRUE(key_encoded_frame != nullptr);
  std::optional<FrameInstrumentationData> frame_inst_data_key_frame =
      key_encoded_frame->CodecSpecific()->frame_instrumentation_data;
  ASSERT_TRUE(frame_inst_data_key_frame.has_value());
  EXPECT_EQ(frame_inst_data_key_frame->sequence_index(), 0);
  EXPECT_THAT(frame_inst_data_key_frame->std_dev(), DoubleNear(kStd, 0.1));
  EXPECT_EQ(frame_inst_data_key_frame->luma_error_threshold(), kLumaThreshold);
  EXPECT_EQ(frame_inst_data_key_frame->chroma_error_threshold(),
            kChormaThreshold);

  mock_on_complete_frame_callback_.ClearExpectedBitstream();
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kDeltaFramePayload);

  EXPECT_TRUE(delta_frame_packet.GetExtension<CorruptionDetectionExtension>());
  std::unique_ptr<EncodedFrame> delta_encoded_frame;
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_))
      .WillOnce([&](EncodedFrame* encoded_frame) {
        delta_encoded_frame = std::make_unique<EncodedFrame>(*encoded_frame);
      });
  rtp_video_stream_receiver_->OnRtpPacket(delta_frame_packet);
  ASSERT_TRUE(delta_encoded_frame != nullptr);
  ASSERT_TRUE(delta_encoded_frame->CodecSpecific()
                  ->frame_instrumentation_data.has_value());

  std::optional<FrameInstrumentationData> frame_inst_data_delta_frame =
      delta_encoded_frame->CodecSpecific()->frame_instrumentation_data;
  ASSERT_TRUE(frame_inst_data_delta_frame.has_value());
  EXPECT_EQ(frame_inst_data_delta_frame->sequence_index(), kNumSamples);
  EXPECT_THAT(frame_inst_data_delta_frame->std_dev(), DoubleNear(kStd, 0.1));
  EXPECT_EQ(frame_inst_data_delta_frame->luma_error_threshold(),
            kLumaThreshold);
  EXPECT_EQ(frame_inst_data_delta_frame->chroma_error_threshold(),
            kChormaThreshold);
}

TEST_F(RtpVideoStreamReceiver2Test,
       FrameInstrumentationDataGetsPopulatedForSyncMessage) {
  const std::vector<uint8_t> kKeyFramePayload = {0, 1, 2, 3, 4};

  // Prepare the receiver for VP9.
  CodecParameterMap codec_params;
  rtp_video_stream_receiver_->AddReceiveCodec(kVp9PayloadType, kVideoCodecVP9,
                                              codec_params,
                                              /*raw_payload=*/false);

  ReceivedPacketGenerator received_packet_generator;
  // Create a sync-only message.
  CorruptionDetectionMessage::Builder builder;
  builder.WithSequenceIndex(0).WithInterpretSequenceIndexAsMostSignificantBits(
      true);
  std::optional<CorruptionDetectionMessage> corruption_detection_msg =
      builder.Build();
  ASSERT_TRUE(corruption_detection_msg.has_value());
  received_packet_generator.SetCorruptionDetectionHeader(
      *corruption_detection_msg);

  // Generate key frame packets.
  received_packet_generator.SetPayload(kKeyFramePayload,
                                       VideoFrameType::kVideoFrameKey);
  // Have corruption header on the key frame.
  RtpPacketReceived key_frame_packet =
      received_packet_generator.NextPacket(/*include_corruption_header=*/true);

  rtp_video_stream_receiver_->StartReceive();
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kKeyFramePayload);

  EXPECT_TRUE(key_frame_packet.GetExtension<CorruptionDetectionExtension>());
  std::unique_ptr<EncodedFrame> key_encoded_frame;
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_))
      .WillOnce([&](EncodedFrame* encoded_frame) {
        key_encoded_frame = std::make_unique<EncodedFrame>(*encoded_frame);
      });
  rtp_video_stream_receiver_->OnRtpPacket(key_frame_packet);
  ASSERT_TRUE(key_encoded_frame != nullptr);
  std::optional<FrameInstrumentationData> frame_inst_data_key_frame =
      key_encoded_frame->CodecSpecific()->frame_instrumentation_data;
  ASSERT_TRUE(frame_inst_data_key_frame.has_value());
  EXPECT_EQ(frame_inst_data_key_frame->sequence_index(), 0);
  EXPECT_TRUE(frame_inst_data_key_frame->is_sync_only());
  EXPECT_EQ(frame_inst_data_key_frame->std_dev(), 0.0);
  EXPECT_EQ(frame_inst_data_key_frame->luma_error_threshold(), 0);
  EXPECT_EQ(frame_inst_data_key_frame->chroma_error_threshold(), 0);
  EXPECT_TRUE(frame_inst_data_key_frame->sample_values().empty());
}

TEST_F(RtpVideoStreamReceiver2Test,
       FrameInstrumentationDataGetsPopulatedMSBIncreasedCorrectly) {
  const std::vector<uint8_t> kKeyFramePayload = {0, 1, 2, 3, 4};
  const std::vector<uint8_t> kDeltaFramePayload = {5, 6, 7, 8, 9};

  // Prepare the receiver for VP9.
  CodecParameterMap codec_params;
  rtp_video_stream_receiver_->AddReceiveCodec(kVp9PayloadType, kVideoCodecVP9,
                                              codec_params,
                                              /*raw_payload=*/false);

  ReceivedPacketGenerator received_packet_generator;
  std::optional<CorruptionDetectionMessage> corruption_detection_msg =
      GetCorruptionDetectionMessage(
          /*sequence_idx=*/0, /*interpret_as_MSB*/ true);
  ASSERT_TRUE(corruption_detection_msg.has_value());
  received_packet_generator.SetCorruptionDetectionHeader(
      *corruption_detection_msg);

  // Generate key frame packets.
  received_packet_generator.SetPayload(kKeyFramePayload,
                                       VideoFrameType::kVideoFrameKey);
  // Have corruption header on the key frame.
  RtpPacketReceived key_frame_packet =
      received_packet_generator.NextPacket(/*include_corruption_header=*/true);
  rtp_video_stream_receiver_->StartReceive();
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kKeyFramePayload);
  rtp_video_stream_receiver_->OnRtpPacket(key_frame_packet);

  RtpPacketReceived delta_frame_packet;
  int sequence_idx = 0;
  for (int i = 0; i < 10; i++) {
    sequence_idx += kNumSamples;
    if (sequence_idx > kMaxSequenceIdx) {
      sequence_idx = sequence_idx - (kMaxSequenceIdx + 1);
    }
    corruption_detection_msg = GetCorruptionDetectionMessage(
        /*sequence_idx=*/sequence_idx, /*interpret_as_MSB*/ false);
    ASSERT_TRUE(corruption_detection_msg.has_value());
    received_packet_generator.SetCorruptionDetectionHeader(
        *corruption_detection_msg);

    // Generate delta frame packet.
    received_packet_generator.SetPayload(kDeltaFramePayload,
                                         VideoFrameType::kVideoFrameDelta);
    // Send corruption header with each frame.
    delta_frame_packet = received_packet_generator.NextPacket(
        /*include_corruption_header=*/true);

    mock_on_complete_frame_callback_.ClearExpectedBitstream();
    mock_on_complete_frame_callback_.AppendExpectedBitstream(
        kDeltaFramePayload);

    EXPECT_TRUE(
        delta_frame_packet.GetExtension<CorruptionDetectionExtension>());
    std::unique_ptr<EncodedFrame> delta_encoded_frame;
    EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_))
        .WillOnce([&](EncodedFrame* encoded_frame) {
          delta_encoded_frame = std::make_unique<EncodedFrame>(*encoded_frame);
        });
    rtp_video_stream_receiver_->OnRtpPacket(delta_frame_packet);
    ASSERT_TRUE(delta_encoded_frame != nullptr);
    std::optional<FrameInstrumentationData> frame_inst_data =
        delta_encoded_frame->CodecSpecific()->frame_instrumentation_data;
    ASSERT_TRUE(frame_inst_data.has_value());
    if (frame_inst_data->sequence_index() < (kMaxSequenceIdx + 1)) {
      EXPECT_EQ(frame_inst_data->sequence_index(), sequence_idx);
    } else {
      EXPECT_EQ(frame_inst_data->sequence_index(),
                sequence_idx + kMaxSequenceIdx + 1);
    }
  }
}

// TODO: bugs.webrtc.org/358039777 - Add tests for corruption detection when we
// have scalability.

TEST_F(RtpVideoStreamReceiver2Test, GenericKeyFrame) {
  RtpPacketReceived rtp_packet;
  CopyOnWriteBuffer data("1234");
  rtp_packet.SetPayloadType(kPayloadType);
  rtp_packet.SetSequenceNumber(1);
  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameKey);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(data, rtp_packet,
                                                              video_header);
}

TEST_F(RtpVideoStreamReceiver2Test, SetProtectionPayloadTypes) {
  EXPECT_NE(rtp_video_stream_receiver_->red_payload_type(), 104);
  EXPECT_NE(rtp_video_stream_receiver_->ulpfec_payload_type(), 107);

  rtp_video_stream_receiver_->SetProtectionPayloadTypes(104, 107);

  EXPECT_EQ(rtp_video_stream_receiver_->red_payload_type(), 104);
  EXPECT_EQ(rtp_video_stream_receiver_->ulpfec_payload_type(), 107);
}

TEST_F(RtpVideoStreamReceiver2Test, PacketInfoIsPropagatedIntoVideoFrames) {
  constexpr uint64_t kAbsoluteCaptureTimestamp = 12;
  constexpr RtpHeaderExtensionId kId0(1);

  RtpHeaderExtensionMap extension_map;
  extension_map.Register<AbsoluteCaptureTimeExtension>(kId0);
  RtpPacketReceived rtp_packet(&extension_map);
  rtp_packet.SetPayloadType(kPayloadType);
  CopyOnWriteBuffer data("1234");
  rtp_packet.SetSequenceNumber(1);
  rtp_packet.SetTimestamp(1);
  rtp_packet.SetSsrc(kSsrc);
  rtp_packet.SetExtension<AbsoluteCaptureTimeExtension>(AbsoluteCaptureTime{
      .absolute_capture_timestamp = kAbsoluteCaptureTimestamp,
      .estimated_capture_clock_offset = std::nullopt});

  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameKey);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_))
      .WillOnce([kAbsoluteCaptureTimestamp](EncodedFrame* frame) {
        EXPECT_THAT(GetAbsoluteCaptureTimestamps(frame),
                    ElementsAre(kAbsoluteCaptureTimestamp));
      });
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(data, rtp_packet,
                                                              video_header);
}

TEST_F(RtpVideoStreamReceiver2Test,
       MissingAbsoluteCaptureTimeIsFilledWithExtrapolatedValue) {
  constexpr uint64_t kAbsoluteCaptureTimestamp = 12;
  constexpr RtpHeaderExtensionId kId0(1);

  RtpHeaderExtensionMap extension_map;
  extension_map.Register<AbsoluteCaptureTimeExtension>(kId0);
  RtpPacketReceived rtp_packet(&extension_map);
  rtp_packet.SetPayloadType(kPayloadType);

  CopyOnWriteBuffer data("1234");
  uint16_t sequence_number = 1;
  uint32_t rtp_timestamp = 1;
  rtp_packet.SetSequenceNumber(sequence_number);
  rtp_packet.SetTimestamp(rtp_timestamp);
  rtp_packet.SetSsrc(kSsrc);
  rtp_packet.SetExtension<AbsoluteCaptureTimeExtension>(AbsoluteCaptureTime{
      .absolute_capture_timestamp = kAbsoluteCaptureTimestamp,
      .estimated_capture_clock_offset = std::nullopt});

  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameKey);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(data, rtp_packet,
                                                              video_header);

  // Rtp packet without absolute capture time.
  rtp_packet = RtpPacketReceived(&extension_map);
  rtp_packet.SetPayloadType(kPayloadType);
  rtp_packet.SetSequenceNumber(++sequence_number);
  rtp_packet.SetTimestamp(++rtp_timestamp);
  rtp_packet.SetSsrc(kSsrc);

  // There is no absolute capture time in the second packet.
  // Expect rtp video stream receiver to extrapolate it for the resulting video
  // frame using absolute capture time from the previous packet.
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_))
      .WillOnce([](EncodedFrame* frame) {
        EXPECT_THAT(GetAbsoluteCaptureTimestamps(frame), SizeIs(1));
      });
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(data, rtp_packet,
                                                              video_header);
}

// FrameDecoded() advances PacketBuffer's cleared boundary. Verify that a
// recovery keyframe whose sequence number unwraps below that boundary starts a
// new receive epoch instead of being discarded as already cleared.
TEST_F(RtpVideoStreamReceiver2Test, RecoversAfterLargeSequenceNumberJump) {
  const CopyOnWriteBuffer data("1234");
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);

  auto inject = [&](uint16_t seq, uint32_t rtp_timestamp, bool keyframe) {
    RtpPacketReceived rtp_packet;
    rtp_packet.SetPayloadType(kPayloadType);
    rtp_packet.SetSequenceNumber(seq);
    rtp_packet.SetTimestamp(rtp_timestamp);
    rtp_packet.SetSsrc(kSsrc);
    RTPVideoHeader video_header =
        GetGenericVideoHeader(keyframe ? VideoFrameType::kVideoFrameKey
                                       : VideoFrameType::kVideoFrameDelta);
    rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
        data, rtp_packet, video_header);
  };

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .Times(7)
      .WillRepeatedly([&](EncodedFrame* frame) {
        rtp_video_stream_receiver_->FrameDecoded(frame->Id());
      });

  uint16_t seq = 60'000;
  uint32_t ts = 90'000;
  inject(seq, ts, /*keyframe=*/true);
  for (int i = 0; i < 3; ++i) {
    inject(++seq, ts += 3'000, /*keyframe=*/false);
  }

  // The sender advances by more than half the 16-bit sequence number range.
  seq += 40'000;
  ts += 40'000u * 3'000u;
  inject(seq, ts, /*keyframe=*/true);
  for (int i = 0; i < 2; ++i) {
    inject(++seq, ts += 3'000, /*keyframe=*/false);
  }

  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(0));
}

TEST_F(RtpVideoStreamReceiver2Test,
       RetriesKeyframeRequestAfterLargeSequenceNumberJump) {
  const CopyOnWriteBuffer data("1234");
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);

  auto inject = [&](uint16_t seq, uint32_t rtp_timestamp, bool keyframe,
                    bool starts_frame = true) {
    RtpPacketReceived rtp_packet;
    rtp_packet.SetPayloadType(kPayloadType);
    rtp_packet.SetSequenceNumber(seq);
    rtp_packet.SetTimestamp(rtp_timestamp);
    rtp_packet.SetSsrc(kSsrc);
    RTPVideoHeader video_header =
        GetGenericVideoHeader(keyframe ? VideoFrameType::kVideoFrameKey
                                       : VideoFrameType::kVideoFrameDelta);
    video_header.is_first_packet_in_frame = starts_frame;
    rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
        data, rtp_packet, video_header);
  };

  int num_complete_frames = 0;
  ON_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillByDefault([&](EncodedFrame* frame) {
        ++num_complete_frames;
        rtp_video_stream_receiver_->FrameDecoded(frame->Id());
      });

  uint16_t seq = 60'000;
  uint32_t ts = 90'000;
  inject(seq, ts, /*keyframe=*/true);
  EXPECT_THAT(num_complete_frames, Eq(1));

  seq += 40'000;
  ts += 40'000u * 3'000u;
  // The first packet after the outage is the tail of a frame. Discontinuity
  // detection must not depend on is_first_packet_in_frame.
  inject(seq, ts, /*keyframe=*/false, /*starts_frame=*/false);
  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(1));
  EXPECT_THAT(num_complete_frames, Eq(1));

  // Recovery is already pending. Delta frames are discarded without sending
  // another request before the retry interval expires.
  time_controller_.AdvanceTime(kMaxWaitForKeyframe - TimeDelta::Millis(1));
  inject(++seq, ts += 3'000, /*keyframe=*/false);
  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(1));
  EXPECT_THAT(num_complete_frames, Eq(1));

  // A reordered keyframe from the old timestamp epoch must not end recovery.
  inject(50'000, 80'000, /*keyframe=*/true);
  EXPECT_THAT(num_complete_frames, Eq(1));

  // Treat the first request as lost. A later delta frame triggers a retry.
  time_controller_.AdvanceTime(TimeDelta::Millis(2));
  inject(++seq, ts += 3'000, /*keyframe=*/false);
  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(2));
  EXPECT_THAT(num_complete_frames, Eq(1));

  // The keyframe received after the retried request resumes frame delivery.
  inject(++seq, ts += 3'000, /*keyframe=*/true);
  EXPECT_THAT(num_complete_frames, Eq(2));
  inject(++seq, ts += 3'000, /*keyframe=*/false);
  EXPECT_THAT(num_complete_frames, Eq(3));
}

TEST_F(RtpVideoStreamReceiver2Test,
       DoesNotResetForSingleSeverelyReorderedOldPacket) {
  const CopyOnWriteBuffer data("1234");
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);

  auto inject = [&](uint16_t seq, uint32_t rtp_timestamp, bool keyframe) {
    RtpPacketReceived rtp_packet;
    rtp_packet.SetPayloadType(kPayloadType);
    rtp_packet.SetSequenceNumber(seq);
    rtp_packet.SetTimestamp(rtp_timestamp);
    rtp_packet.SetSsrc(kSsrc);
    rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
        data, rtp_packet,
        GetGenericVideoHeader(keyframe ? VideoFrameType::kVideoFrameKey
                                       : VideoFrameType::kVideoFrameDelta));
  };

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .Times(3)
      .WillRepeatedly([&](EncodedFrame* frame) {
        rtp_video_stream_receiver_->FrameDecoded(frame->Id());
      });

  inject(60'000, 90'000, /*keyframe=*/true);
  inject(60'001, 93'000, /*keyframe=*/false);

  // Its timestamp identifies it as old media, so this packet must not turn a
  // reordering event into a stream reset.
  inject(30'000, 80'000, /*keyframe=*/false);
  inject(60'002, 96'000, /*keyframe=*/false);
  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(0));
}

// The packet with the newest sequence number does not necessarily have the
// newest RTP timestamp. A keyframe with an already received RTP timestamp and a
// sequence number far behind the newest one must not end the receive epoch,
// since it would then be delivered again with a new frame id.
TEST_F(RtpVideoStreamReceiver2Test,
       DoesNotResetForKeyframeWithAlreadyReceivedTimestamp) {
  const CopyOnWriteBuffer data("1234");
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);

  auto inject = [&](uint16_t seq, uint32_t rtp_timestamp, bool keyframe) {
    RtpPacketReceived rtp_packet;
    rtp_packet.SetPayloadType(kPayloadType);
    rtp_packet.SetSequenceNumber(seq);
    rtp_packet.SetTimestamp(rtp_timestamp);
    rtp_packet.SetSsrc(kSsrc);
    rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
        data, rtp_packet,
        GetGenericVideoHeader(keyframe ? VideoFrameType::kVideoFrameKey
                                       : VideoFrameType::kVideoFrameDelta));
  };

  std::vector<uint32_t> complete_frame_timestamps;
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillRepeatedly([&](EncodedFrame* frame) {
        complete_frame_timestamps.push_back(frame->RtpTimestamp());
        rtp_video_stream_receiver_->FrameDecoded(frame->Id());
      });

  inject(60'000, 90'000, /*keyframe=*/true);
  // The newest sequence number arrives with an older RTP timestamp.
  inject(60'001, 87'000, /*keyframe=*/false);
  // A copy of the first keyframe, far behind the newest sequence number.
  inject(57'000, 90'000, /*keyframe=*/true);
  inject(60'002, 93'000, /*keyframe=*/false);

  EXPECT_THAT(complete_frame_timestamps, ElementsAre(90'000, 87'000, 93'000));
  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(0));
}

TEST_F(RtpVideoStreamReceiver2Test,
       PaddingDoesNotMoveUnwrapperAcrossLargeDiscontinuity) {
  const CopyOnWriteBuffer data("1234");
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);
  rtp_video_stream_receiver_->StartReceive();

  auto inject = [&](uint16_t seq, uint32_t rtp_timestamp, bool keyframe) {
    RtpPacketReceived rtp_packet;
    rtp_packet.SetPayloadType(kPayloadType);
    rtp_packet.SetSequenceNumber(seq);
    rtp_packet.SetTimestamp(rtp_timestamp);
    rtp_packet.SetSsrc(kSsrc);
    rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
        data, rtp_packet,
        GetGenericVideoHeader(keyframe ? VideoFrameType::kVideoFrameKey
                                       : VideoFrameType::kVideoFrameDelta));
  };

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .Times(3)
      .WillRepeatedly([&](EncodedFrame* frame) {
        rtp_video_stream_receiver_->FrameDecoded(frame->Id());
      });

  inject(60'000, 90'000, /*keyframe=*/true);

  // Padding is the first packet observed after the outage. It must be ignored
  // without committing the ambiguous sequence-number epoch.
  RtpPacketReceived padding;
  padding.SetPayloadType(kPayloadType);
  padding.SetSequenceNumber(34'464);
  padding.SetTimestamp(120'090'000);
  padding.SetSsrc(kSsrc);
  rtp_video_stream_receiver_->OnRtpPacket(padding);
  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(0));

  inject(34'465, 120'093'000, /*keyframe=*/true);
  inject(34'466, 120'096'000, /*keyframe=*/false);
}

TEST_F(RtpVideoStreamReceiver2Test,
       DoesNotEnterRecoveryForNormalSequenceNumberWrap) {
  const CopyOnWriteBuffer data("1234");
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .Times(4)
      .WillRepeatedly([&](EncodedFrame* frame) {
        rtp_video_stream_receiver_->FrameDecoded(frame->Id());
      });

  uint32_t ts = 90'000;
  for (uint16_t seq :
       {uint16_t{65'534}, uint16_t{65'535}, uint16_t{0}, uint16_t{1}}) {
    RtpPacketReceived rtp_packet;
    rtp_packet.SetPayloadType(kPayloadType);
    rtp_packet.SetSequenceNumber(seq);
    rtp_packet.SetTimestamp(ts);
    rtp_packet.SetSsrc(kSsrc);
    rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
        data, rtp_packet,
        GetGenericVideoHeader(seq == 65'534
                                  ? VideoFrameType::kVideoFrameKey
                                  : VideoFrameType::kVideoFrameDelta));
    ts += 3'000;
  }
  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(0));
}

TEST_F(RtpVideoStreamReceiver2Test,
       StaleFrameDecodedAfterRecoveryDoesNotClearNewSequenceEpoch) {
  const CopyOnWriteBuffer data("1234");
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);
  int64_t stale_picture_id = -1;

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce([&](EncodedFrame* frame) {
        rtp_video_stream_receiver_->FrameDecoded(frame->Id());
      })
      .WillOnce([&](EncodedFrame* frame) { stale_picture_id = frame->Id(); })
      .WillOnce([&](EncodedFrame* frame) {
        rtp_video_stream_receiver_->FrameDecoded(frame->Id());
      })
      .WillOnce([&](EncodedFrame* frame) {
        rtp_video_stream_receiver_->FrameDecoded(frame->Id());
      });

  auto inject = [&](uint16_t seq, uint32_t rtp_timestamp, bool keyframe) {
    RtpPacketReceived rtp_packet;
    rtp_packet.SetPayloadType(kPayloadType);
    rtp_packet.SetSequenceNumber(seq);
    rtp_packet.SetTimestamp(rtp_timestamp);
    rtp_packet.SetSsrc(kSsrc);
    rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
        data, rtp_packet,
        GetGenericVideoHeader(keyframe ? VideoFrameType::kVideoFrameKey
                                       : VideoFrameType::kVideoFrameDelta));
  };

  uint16_t seq = 60'000;
  uint32_t ts = 90'000;
  inject(seq, ts, /*keyframe=*/true);
  inject(++seq, ts += 3'000, /*keyframe=*/false);

  seq += 40'000;
  ts += 40'000u * 3'000u;
  inject(seq, ts, /*keyframe=*/true);
  ASSERT_NE(stale_picture_id, -1);

  // Recovery cleared the old frame history, so this delayed callback cannot
  // move PacketBuffer::ClearTo() into the new epoch.
  rtp_video_stream_receiver_->FrameDecoded(stale_picture_id);
  inject(++seq, ts += 3'000, /*keyframe=*/false);
}

TEST_F(RtpVideoStreamReceiver2Test,
       DelayedVp9FrameDecodedDoesNotClearPastNewestReceivedPacket) {
  const CopyOnWriteBuffer data("1234");
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);

  int num_complete_frames = 0;
  int64_t delayed_picture_id = -1;
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .Times(5)
      .WillRepeatedly([&](EncodedFrame* frame) {
        ++num_complete_frames;
        if (delayed_picture_id == -1) {
          delayed_picture_id = frame->Id();
        } else {
          EXPECT_LT(frame->Id(), delayed_picture_id);
          rtp_video_stream_receiver_->FrameDecoded(frame->Id());
        }
      });

  auto inject_keyframe = [&](uint16_t seq, uint32_t rtp_timestamp,
                             int picture_id) {
    RtpPacketReceived rtp_packet;
    rtp_packet.SetPayloadType(kPayloadType);
    rtp_packet.SetSequenceNumber(seq);
    rtp_packet.SetTimestamp(rtp_timestamp);
    rtp_packet.SetSsrc(kSsrc);

    RTPVideoHeader video_header;
    video_header.codec = kVideoCodecVP9;
    video_header.frame_type = VideoFrameType::kVideoFrameKey;
    video_header.is_first_packet_in_frame = true;
    video_header.is_last_packet_in_frame = true;
    RTPVideoHeaderVP9 vp9_header;
    vp9_header.InitRTPVideoHeaderVP9();
    vp9_header.flexible_mode = true;
    vp9_header.picture_id = picture_id;
    vp9_header.temporal_idx = 0;
    vp9_header.spatial_idx = 0;
    video_header.video_type_header = vp9_header;
    rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
        data, rtp_packet, video_header);
  };

  // A smaller Picture ID space arrives under the same SSRC. Keep the old
  // frame undecoded while the new frames are decoded and RTP sequence numbers
  // advance normally by more than half of their 16-bit range.
  inject_keyframe(20'352, 740'278'071, 13'600);
  inject_keyframe(40'000, 740'281'071, 100);
  inject_keyframe(60'000, 740'284'071, 101);
  inject_keyframe(61'934, 740'285'921, 102);
  ASSERT_THAT(num_complete_frames, Eq(4));
  ASSERT_NE(delayed_picture_id, -1);

  // Unwrapping the delayed frame's sequence number relative to 61934 must not
  // move PacketBuffer's cleared boundary into the future sequence epoch.
  rtp_video_stream_receiver_->FrameDecoded(delayed_picture_id);
  inject_keyframe(61'935, 740'288'921, 103);
  EXPECT_THAT(num_complete_frames, Eq(5));
}

TEST_F(RtpVideoStreamReceiver2Test,
       NoInfiniteRecursionOnEncapsulatedRedPacket) {
  const std::vector<uint8_t> data({
      0x80,              // RTP version.
      kRedPayloadType,   // Payload type.
      0, 0, 0, 0, 0, 0,  // Don't care.
      0, 0, 0x4, 0x57,   // SSRC
      kRedPayloadType,   // RED header.
      0, 0, 0, 0, 0      // Don't care.
  });
  RtpPacketReceived packet;
  EXPECT_TRUE(packet.Parse(data.data(), data.size()));
  rtp_video_stream_receiver_->StartReceive();
  rtp_video_stream_receiver_->OnRtpPacket(packet);
}

TEST_F(RtpVideoStreamReceiver2Test,
       DropsPacketWithRedPayloadTypeAndEmptyPayload) {
  config_.rtp.red_payload_type = kRedPayloadType;
  SetUp();  // re-create rtp_video_stream_receiver with red payload type.
  // clang-format off
  const uint8_t data[] = {
      0x80,              // RTP version.
      kRedPayloadType,   // Payload type.
      0, 0, 0, 0, 0, 0,  // Don't care.
      0, 0, 0x4, 0x57,   // SSRC
      // Empty rtp payload.
  };
  // clang-format on
  RtpPacketReceived packet;
  // Manually convert to CopyOnWriteBuffer to be sure capacity == size
  // and asan bot can catch read buffer overflow.
  EXPECT_TRUE(packet.Parse(CopyOnWriteBuffer(data)));
  rtp_video_stream_receiver_->StartReceive();
  rtp_video_stream_receiver_->OnRtpPacket(packet);
  // Expect asan doesn't find anything.
}

TEST_F(RtpVideoStreamReceiver2Test, GenericKeyFrameBitstreamError) {
  RtpPacketReceived rtp_packet;
  rtp_packet.SetPayloadType(kPayloadType);
  CopyOnWriteBuffer data("1234");
  rtp_packet.SetSequenceNumber(1);
  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameKey);
  constexpr uint8_t expected_bitsteam[] = {1, 2, 3, 0xff};
  mock_on_complete_frame_callback_.AppendExpectedBitstream(expected_bitsteam);
  EXPECT_CALL(mock_on_complete_frame_callback_,
              DoOnCompleteFrameFailBitstream(_));
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(data, rtp_packet,
                                                              video_header);
}

class RtpVideoStreamReceiver2TestH264
    : public RtpVideoStreamReceiver2Test,
      public ::testing::WithParamInterface<std::string> {
 protected:
  RtpVideoStreamReceiver2TestH264() : RtpVideoStreamReceiver2Test(GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(SpsPpsIdrIsKeyframeAndH26xPacketBuffer,
                         RtpVideoStreamReceiver2TestH264,
                         Values("",
                                "WebRTC-SpsPpsIdrIsH264Keyframe/Enabled/",
                                "WebRTC-Video-H26xPacketBuffer/Enabled/",
                                "WebRTC-SpsPpsIdrIsH264Keyframe/Enabled/"
                                "WebRTC-Video-H26xPacketBuffer/Enabled/"));

TEST_P(RtpVideoStreamReceiver2TestH264, InBandSpsPps) {
  constexpr int kH264PayloadType = 98;
  CodecParameterMap codec_params;
  rtp_video_stream_receiver_->AddReceiveCodec(kH264PayloadType, kVideoCodecH264,
                                              codec_params,
                                              /*raw_payload=*/false);
  rtp_video_stream_receiver_->StartReceive();

  CopyOnWriteBuffer sps_data;
  RtpPacketReceived rtp_packet;
  RTPVideoHeader sps_video_header = GetDefaultH264VideoHeader();
  AddSps(&sps_video_header, 0, &sps_data);
  rtp_packet.SetSequenceNumber(0);
  rtp_packet.SetPayloadType(kH264PayloadType);
  sps_video_header.is_first_packet_in_frame = true;
  sps_video_header.frame_type = VideoFrameType::kEmptyFrame;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(sps_data);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      sps_data, rtp_packet, sps_video_header);

  CopyOnWriteBuffer pps_data;
  RTPVideoHeader pps_video_header = GetDefaultH264VideoHeader();
  AddPps(&pps_video_header, 0, 1, &pps_data);
  rtp_packet.SetSequenceNumber(1);
  rtp_packet.SetPayloadType(kH264PayloadType);
  pps_video_header.is_first_packet_in_frame = true;
  pps_video_header.frame_type = VideoFrameType::kEmptyFrame;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(pps_data);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      pps_data, rtp_packet, pps_video_header);

  RTPVideoHeader idr_video_header = GetDefaultH264VideoHeader();
  AddIdr(&idr_video_header, 1);
  rtp_packet.SetSequenceNumber(2);
  rtp_packet.SetPayloadType(kH264PayloadType);
  rtp_packet.SetMarker(true);
  idr_video_header.is_first_packet_in_frame = true;
  idr_video_header.is_last_packet_in_frame = true;
  idr_video_header.frame_type = VideoFrameType::kVideoFrameKey;
  const uint8_t idr[] = {0x65, 1, 2, 3};
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(idr);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      CopyOnWriteBuffer(idr), rtp_packet, idr_video_header);
}

TEST_P(RtpVideoStreamReceiver2TestH264, OutOfBandFmtpSpsPps) {
  constexpr int kH264PayloadType = 99;
  CodecParameterMap codec_params;
  // Example parameter sets from https://tools.ietf.org/html/rfc3984#section-8.2
  // .
  codec_params.emplace(kH264FmtpSpropParameterSets, "Z0IACpZTBYmI,aMljiA==");
  rtp_video_stream_receiver_->AddReceiveCodec(kH264PayloadType, kVideoCodecH264,
                                              codec_params,
                                              /*raw_payload=*/false);
  rtp_video_stream_receiver_->StartReceive();
  const uint8_t binary_sps[] = {0x67, 0x42, 0x00, 0x0a, 0x96,
                                0x53, 0x05, 0x89, 0x88};
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(binary_sps);
  const uint8_t binary_pps[] = {0x68, 0xc9, 0x63, 0x88};
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(binary_pps);

  RtpPacketReceived rtp_packet;
  RTPVideoHeader video_header = GetDefaultH264VideoHeader();
  AddIdr(&video_header, 0);
  rtp_packet.SetPayloadType(kH264PayloadType);
  rtp_packet.SetSequenceNumber(2);
  rtp_packet.SetMarker(true);
  video_header.is_first_packet_in_frame = true;
  video_header.is_last_packet_in_frame = true;
  video_header.codec = kVideoCodecH264;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;
  CopyOnWriteBuffer data("123");
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);
  // IDR frames without SPS/PPS are not returned by
  // |H26xPacketBuffer.InsertPacket| until SPS and PPS are received when
  // WebRTC-SpsPpsIdrIsH264Keyframe is enabled.
  if (!env_.field_trials().IsEnabled("WebRTC-SpsPpsIdrIsH264Keyframe") ||
      !env_.field_trials().IsEnabled("WebRTC-Video-H26xPacketBuffer")) {
    EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  }
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(data, rtp_packet,
                                                              video_header);
}

TEST_P(RtpVideoStreamReceiver2TestH264, ForceSpsPpsIdrIsKeyframe) {
  CodecParameterMap codec_params;
  // Forcing can be done either with field trial or codec_params.
  if (!env_.field_trials().IsEnabled("WebRTC-SpsPpsIdrIsH264Keyframe")) {
    codec_params.emplace(kH264FmtpSpsPpsIdrInKeyframe, "");
  }
  rtp_video_stream_receiver_->AddReceiveCodec(kPayloadType, kVideoCodecH264,
                                              codec_params,
                                              /*raw_payload=*/false);
  rtp_video_stream_receiver_->StartReceive();
  CopyOnWriteBuffer sps_data;
  RtpPacketReceived rtp_packet;
  RTPVideoHeader sps_video_header = GetDefaultH264VideoHeader();
  AddSps(&sps_video_header, 0, &sps_data);
  rtp_packet.SetSequenceNumber(0);
  rtp_packet.SetPayloadType(kPayloadType);
  sps_video_header.is_first_packet_in_frame = true;
  sps_video_header.frame_type = VideoFrameType::kEmptyFrame;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(sps_data);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      sps_data, rtp_packet, sps_video_header);

  CopyOnWriteBuffer pps_data;
  RTPVideoHeader pps_video_header = GetDefaultH264VideoHeader();
  AddPps(&pps_video_header, 0, 1, &pps_data);
  rtp_packet.SetSequenceNumber(1);
  pps_video_header.is_first_packet_in_frame = true;
  pps_video_header.frame_type = VideoFrameType::kEmptyFrame;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(pps_data);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      pps_data, rtp_packet, pps_video_header);

  RTPVideoHeader idr_video_header = GetDefaultH264VideoHeader();
  AddIdr(&idr_video_header, 1);
  rtp_packet.SetSequenceNumber(2);
  rtp_packet.SetMarker(true);
  idr_video_header.is_first_packet_in_frame = true;
  idr_video_header.is_last_packet_in_frame = true;
  idr_video_header.frame_type = VideoFrameType::kVideoFrameKey;
  const uint8_t idr[] = {0x65, 1, 2, 3};
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(idr);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_TRUE(frame->is_keyframe()); });
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      CopyOnWriteBuffer(idr), rtp_packet, idr_video_header);
  mock_on_complete_frame_callback_.ClearExpectedBitstream();
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(idr);
  rtp_packet.SetSequenceNumber(3);
  // IDR frames without SPS/PPS are not returned by
  // |H26xPacketBuffer.InsertPacket| until SPS and PPS are received, while
  // |PacketBuffer| returns it as a delta frame.
  if (env_.field_trials().IsEnabled("WebRTC-Video-H26xPacketBuffer")) {
    EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame).Times(0);
  } else {
    EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
        .WillOnce(
            [&](EncodedFrame* frame) { EXPECT_FALSE(frame->is_keyframe()); });
  }
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      CopyOnWriteBuffer(idr), rtp_packet, idr_video_header);
}

class RtpVideoStreamReceiver2TestPadding
    : public RtpVideoStreamReceiver2Test,
      public ::testing::WithParamInterface<std::string> {
 protected:
  RtpVideoStreamReceiver2TestPadding()
      : RtpVideoStreamReceiver2Test(GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(PaddingInMediaStreamAndH26xPacketBuffer,
                         RtpVideoStreamReceiver2TestPadding,
                         Values("", "WebRTC-Video-H26xPacketBuffer/Enabled/"));

TEST_P(RtpVideoStreamReceiver2TestPadding, PaddingInMediaStream) {
  RtpPacketReceived rtp_packet;
  RTPVideoHeader video_header = GetDefaultH264VideoHeader();
  CopyOnWriteBuffer data("123");
  rtp_packet.SetPayloadType(kPayloadType);
  rtp_packet.SetSequenceNumber(2);
  video_header.is_first_packet_in_frame = true;
  video_header.is_last_packet_in_frame = true;
  video_header.codec = kVideoCodecGeneric;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(data, rtp_packet,
                                                              video_header);

  rtp_packet.SetSequenceNumber(3);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting({}, rtp_packet,
                                                              video_header);

  rtp_packet.SetSequenceNumber(4);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  video_header.frame_type = VideoFrameType::kVideoFrameDelta;
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(data, rtp_packet,
                                                              video_header);

  rtp_packet.SetSequenceNumber(6);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(data, rtp_packet,
                                                              video_header);

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  rtp_packet.SetSequenceNumber(5);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting({}, rtp_packet,
                                                              video_header);
}

TEST_P(RtpVideoStreamReceiver2TestPadding, EmptyPaddingInMediaStream) {
  constexpr int kH264PayloadType = 98;
  RtpPacketReceived rtp_packet_idr, rtp_packet_padding, rtp_packet_slice;
  // Example Stap-A packet with SPS, PPS, and IDR.
  std::vector<uint8_t> raw_rtp_with_sps_pps_idr{
      0x80, 0xe2, 0x13, 0xba, 0x87, 0xa0, 0x0a, 0x8a, 0x00, 0x00, 0x6f,
      0x00, 0x78, 0x00, 0x19, 0x67, 0x42, 0x40, 0x29, 0x95, 0xb8, 0x78,
      0x2f, 0xf9, 0x70, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00,
      0x03, 0x00, 0x78, 0x8d, 0xa1, 0xc3, 0x2e, 0x00, 0x04, 0x68, 0xce,
      0x3c, 0x80, 0x00, 0x07, 0x05, 0x88, 0x80, 0x03, 0x53, 0xff, 0xff};
  // Example Empty padding packet next Idr.
  std::vector<uint8_t> raw_rtp_empty_padding{
      0x80, 0x62, 0x13, 0xbb, 0x87, 0xa0, 0x21, 0x0a, 0x00, 0x00, 0x6f, 0x00};
  // Example Single NALU packet with slice.
  std::vector<uint8_t> raw_rtp_slice(
      {0x80, 0xE2, 0x13, 0xbc, 0x87, 0xa0, 0x21, 0x0a, 0x00, 0x00, 0x6f,
       0x00, 0x01, 0x9a, 0x02, 0x3f, 0xc1, 0x48, 0x9a, 0xeb, 0xea, 0xff});

  // Example EncodedFrame with SPS, PPS, and IDR.
  std::vector<uint8_t> expect_frame_with_sps_pps_idr{
      0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x40, 0x29, 0x95, 0xb8, 0x78, 0x2f,
      0xf9, 0x70, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00,
      0x78, 0x8d, 0xa1, 0xc3, 0x2e, 0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x3c,
      0x80, 0x00, 0x00, 0x00, 0x01, 0x05, 0x88, 0x80, 0x03, 0x53, 0xff, 0xff};
  // Example EncodedFrame with slice.
  std::vector<uint8_t> expect_frame_with_slice{0x00, 0x00, 0x00, 0x01, 0x01,
                                               0x9a, 0x02, 0x3f, 0xc1, 0x48,
                                               0x9a, 0xeb, 0xea, 0xff};
  rtp_packet_idr.Parse(raw_rtp_with_sps_pps_idr.data(),
                       raw_rtp_with_sps_pps_idr.size());
  rtp_packet_padding.Parse(raw_rtp_empty_padding.data(),
                           raw_rtp_empty_padding.size());
  rtp_packet_slice.Parse(raw_rtp_slice.data(), raw_rtp_slice.size());

  // Prepare the receiver for H264.
  CodecParameterMap codec_params;
  rtp_video_stream_receiver_->AddReceiveCodec(kH264PayloadType, kVideoCodecH264,
                                              codec_params, false);
  rtp_video_stream_receiver_->StartReceive();

  // Expect IDR frame.
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      expect_frame_with_sps_pps_idr);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));

  rtp_video_stream_receiver_->OnRtpPacket(rtp_packet_idr);

  rtp_video_stream_receiver_->OnRtpPacket(rtp_packet_padding);

  // Expect single NALU frame.
  mock_on_complete_frame_callback_.ClearExpectedBitstream();
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      expect_frame_with_slice);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  rtp_video_stream_receiver_->OnRtpPacket(rtp_packet_slice);
}

TEST_F(RtpVideoStreamReceiver2Test, RequestKeyframeIfFirstFrameIsDelta) {
  RtpPacketReceived rtp_packet;
  rtp_packet.SetPayloadType(kPayloadType);
  CopyOnWriteBuffer data("1234");
  rtp_packet.SetSequenceNumber(1);
  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameDelta);

  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(data, rtp_packet,
                                                              video_header);
  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(1));
}

TEST_F(RtpVideoStreamReceiver2Test, RequestKeyframeWhenPacketBufferGetsFull) {
  constexpr int kPacketBufferMaxSize = 2048;

  RtpPacketReceived rtp_packet;
  rtp_packet.SetPayloadType(kPayloadType);
  CopyOnWriteBuffer data("1234");
  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameDelta);
  // Incomplete frames so that the packet buffer is filling up.
  video_header.is_last_packet_in_frame = false;
  uint16_t start_sequence_number = 1234;
  rtp_packet.SetSequenceNumber(start_sequence_number);
  while (rtp_packet.SequenceNumber() - start_sequence_number <
         kPacketBufferMaxSize) {
    rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
        data, rtp_packet, video_header);
    rtp_packet.SetSequenceNumber(rtp_packet.SequenceNumber() + 2);
  }

  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(data, rtp_packet,
                                                              video_header);
  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(1));
}

TEST_F(RtpVideoStreamReceiver2Test, SinkGetsRtpNotifications) {
  rtp_video_stream_receiver_->StartReceive();

  MockRtpPacketSink test_sink;
  test_packet_sink_ = &test_sink;

  auto rtp_packet = CreateRtpPacketReceived();
  EXPECT_CALL(test_sink, OnRtpPacket(SamePacketAs(*rtp_packet)));

  rtp_video_stream_receiver_->OnRtpPacket(*rtp_packet);

  // Test tear-down.
  rtp_video_stream_receiver_->StopReceive();
  test_packet_sink_ = nullptr;
}

TEST_F(RtpVideoStreamReceiver2Test, NonStartedStreamGetsNoRtpCallbacks) {
  // Explicitly showing that the stream is not in the `started` state,
  // regardless of whether streams start out `started` or `stopped`.
  rtp_video_stream_receiver_->StopReceive();

  MockRtpPacketSink test_sink;
  test_packet_sink_ = &test_sink;

  auto rtp_packet = CreateRtpPacketReceived();
  EXPECT_CALL(test_sink, OnRtpPacket(_)).Times(0);

  rtp_video_stream_receiver_->OnRtpPacket(*rtp_packet);

  test_packet_sink_ = nullptr;
}

TEST_F(RtpVideoStreamReceiver2Test, ParseGenericDescriptorOnePacket) {
  const std::vector<uint8_t> data = {0, 1, 2, 3, 4};
  const int kSpatialIndex = 1;

  rtp_video_stream_receiver_->StartReceive();

  RtpHeaderExtensionMap extension_map;
  extension_map.Register<RtpGenericFrameDescriptorExtension00>(
      RtpHeaderExtensionId(5));
  RtpPacketReceived rtp_packet(&extension_map);
  rtp_packet.SetPayloadType(kPayloadType);

  RtpGenericFrameDescriptor generic_descriptor;
  generic_descriptor.SetFirstPacketInSubFrame(true);
  generic_descriptor.SetLastPacketInSubFrame(true);
  generic_descriptor.SetFrameId(100);
  generic_descriptor.SetSpatialLayersBitmask(1 << kSpatialIndex);
  generic_descriptor.AddFrameDependencyDiff(90);
  generic_descriptor.AddFrameDependencyDiff(80);
  ASSERT_TRUE(rtp_packet.SetExtension<RtpGenericFrameDescriptorExtension00>(
      generic_descriptor));

  rtp_packet.SetPayload(data);
  // The first byte is the header, so we ignore the first byte of `data`.
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      std::span(data).subspan(1));

  rtp_packet.SetMarker(true);
  rtp_packet.SetPayloadType(kPayloadType);
  rtp_packet.SetSequenceNumber(1);

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce([kSpatialIndex](EncodedFrame* frame) {
        EXPECT_EQ(frame->num_references, 2U);
        EXPECT_EQ(frame->references[0], frame->Id() - 90);
        EXPECT_EQ(frame->references[1], frame->Id() - 80);
        EXPECT_EQ(frame->SpatialIndex(), kSpatialIndex);
        EXPECT_THAT(frame->PacketInfos(), SizeIs(1));
      });

  rtp_video_stream_receiver_->OnRtpPacket(rtp_packet);
}

TEST_F(RtpVideoStreamReceiver2Test, ParseGenericDescriptorTwoPackets) {
  const std::vector<uint8_t> data = {0, 1, 2, 3, 4};
  const int kSpatialIndex = 1;

  rtp_video_stream_receiver_->StartReceive();

  RtpHeaderExtensionMap extension_map;
  extension_map.Register<RtpGenericFrameDescriptorExtension00>(
      RtpHeaderExtensionId(5));
  RtpPacketReceived first_packet(&extension_map);

  RtpGenericFrameDescriptor first_packet_descriptor;
  first_packet_descriptor.SetFirstPacketInSubFrame(true);
  first_packet_descriptor.SetLastPacketInSubFrame(false);
  first_packet_descriptor.SetFrameId(100);
  first_packet_descriptor.SetSpatialLayersBitmask(1 << kSpatialIndex);
  first_packet_descriptor.SetResolution(480, 360);
  ASSERT_TRUE(first_packet.SetExtension<RtpGenericFrameDescriptorExtension00>(
      first_packet_descriptor));

  first_packet.SetPayload(data);
  // The first byte is the header, so we ignore the first byte of `data`.
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      std::span(data).subspan(1));

  first_packet.SetPayloadType(kPayloadType);
  first_packet.SetSequenceNumber(1);
  rtp_video_stream_receiver_->OnRtpPacket(first_packet);

  RtpPacketReceived second_packet(&extension_map);
  RtpGenericFrameDescriptor second_packet_descriptor;
  second_packet_descriptor.SetFirstPacketInSubFrame(false);
  second_packet_descriptor.SetLastPacketInSubFrame(true);
  ASSERT_TRUE(second_packet.SetExtension<RtpGenericFrameDescriptorExtension00>(
      second_packet_descriptor));

  second_packet.SetMarker(true);
  second_packet.SetPayloadType(kPayloadType);
  second_packet.SetSequenceNumber(2);

  second_packet.SetPayload(data);
  // The first byte is the header, so we ignore the first byte of `data`.
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      std::span(data).subspan(1));

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce([kSpatialIndex](EncodedFrame* frame) {
        EXPECT_EQ(frame->num_references, 0U);
        EXPECT_EQ(frame->SpatialIndex(), kSpatialIndex);
        EXPECT_EQ(frame->EncodedImage()._encodedWidth, 480u);
        EXPECT_EQ(frame->EncodedImage()._encodedHeight, 360u);
        EXPECT_THAT(frame->PacketInfos(), SizeIs(2));
      });

  rtp_video_stream_receiver_->OnRtpPacket(second_packet);
}

TEST_F(RtpVideoStreamReceiver2Test, ParseGenericDescriptorRawPayload) {
  const std::vector<uint8_t> data = {0, 1, 2, 3, 4};
  const int kRawPayloadType = 123;

  rtp_video_stream_receiver_->AddReceiveCodec(kRawPayloadType,
                                              kVideoCodecGeneric, {},
                                              /*raw_payload=*/true);
  rtp_video_stream_receiver_->StartReceive();

  RtpHeaderExtensionMap extension_map;
  extension_map.Register<RtpGenericFrameDescriptorExtension00>(
      RtpHeaderExtensionId(5));
  RtpPacketReceived rtp_packet(&extension_map);

  RtpGenericFrameDescriptor generic_descriptor;
  generic_descriptor.SetFirstPacketInSubFrame(true);
  generic_descriptor.SetLastPacketInSubFrame(true);
  ASSERT_TRUE(rtp_packet.SetExtension<RtpGenericFrameDescriptorExtension00>(
      generic_descriptor));

  rtp_packet.SetPayload(data);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);

  rtp_packet.SetMarker(true);
  rtp_packet.SetPayloadType(kRawPayloadType);
  rtp_packet.SetSequenceNumber(1);

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame);
  rtp_video_stream_receiver_->OnRtpPacket(rtp_packet);
}

TEST_F(RtpVideoStreamReceiver2Test, SetRawPayloadTypesUpdatesDepacketizer) {
  const std::vector<uint8_t> data = {0, 1, 2, 3, 4};
  const int kTestRawPayloadType = 123;

  rtp_video_stream_receiver_->AddReceiveCodec(
      kTestRawPayloadType, kVideoCodecGeneric, {}, /*raw_payload=*/false);
  rtp_video_stream_receiver_->StartReceive();

  rtp_video_stream_receiver_->SetRawPayloadTypes({kTestRawPayloadType});

  RtpHeaderExtensionMap extension_map;
  extension_map.Register<RtpGenericFrameDescriptorExtension00>(
      RtpHeaderExtensionId(5));
  RtpPacketReceived rtp_packet(&extension_map);

  RtpGenericFrameDescriptor generic_descriptor;
  generic_descriptor.SetFirstPacketInSubFrame(true);
  generic_descriptor.SetLastPacketInSubFrame(true);
  ASSERT_TRUE(rtp_packet.SetExtension<RtpGenericFrameDescriptorExtension00>(
      generic_descriptor));

  rtp_packet.SetPayload(data);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);

  rtp_packet.SetMarker(true);
  rtp_packet.SetPayloadType(kTestRawPayloadType);
  rtp_packet.SetSequenceNumber(1);

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame);
  rtp_video_stream_receiver_->OnRtpPacket(rtp_packet);
}

TEST_F(RtpVideoStreamReceiver2Test, UnwrapsFrameId) {
  const std::vector<uint8_t> data = {0, 1, 2, 3, 4};
  const int kPayloadTypeGeneric = 123;

  rtp_video_stream_receiver_->AddReceiveCodec(kPayloadTypeGeneric,
                                              kVideoCodecGeneric, {},
                                              /*raw_payload=*/true);
  rtp_video_stream_receiver_->StartReceive();
  RtpHeaderExtensionMap extension_map;
  extension_map.Register<RtpGenericFrameDescriptorExtension00>(
      RtpHeaderExtensionId(5));

  uint16_t rtp_sequence_number = 1;
  auto inject_packet = [&](uint16_t wrapped_frame_id) {
    RtpPacketReceived rtp_packet(&extension_map);

    RtpGenericFrameDescriptor generic_descriptor;
    generic_descriptor.SetFirstPacketInSubFrame(true);
    generic_descriptor.SetLastPacketInSubFrame(true);
    generic_descriptor.SetFrameId(wrapped_frame_id);
    ASSERT_TRUE(rtp_packet.SetExtension<RtpGenericFrameDescriptorExtension00>(
        generic_descriptor));

    rtp_packet.SetPayload(data);
    mock_on_complete_frame_callback_.ClearExpectedBitstream();
    mock_on_complete_frame_callback_.AppendExpectedBitstream(data);
    rtp_packet.SetMarker(true);
    rtp_packet.SetPayloadType(kPayloadTypeGeneric);
    rtp_packet.SetSequenceNumber(++rtp_sequence_number);
    rtp_video_stream_receiver_->OnRtpPacket(rtp_packet);
  };

  int64_t first_picture_id;
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce([&](EncodedFrame* frame) { first_picture_id = frame->Id(); });
  inject_packet(/*wrapped_frame_id=*/0xffff);

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce([&](EncodedFrame* frame) {
        EXPECT_EQ(frame->Id() - first_picture_id, 3);
      });
  inject_packet(/*wrapped_frame_id=*/0x0002);
}

class RtpVideoStreamReceiver2DependencyDescriptorTest
    : public RtpVideoStreamReceiver2Test {
 public:
  RtpVideoStreamReceiver2DependencyDescriptorTest() {
    rtp_video_stream_receiver_->AddReceiveCodec(payload_type_,
                                                kVideoCodecGeneric, {},
                                                /*raw_payload=*/true);
    extension_map_.Register<RtpDependencyDescriptorExtension>(
        RtpHeaderExtensionId(7));
    rtp_video_stream_receiver_->StartReceive();
  }

  // Returns some valid structure for the DependencyDescriptors.
  // First template of that structure always fit for a key frame.
  static FrameDependencyStructure CreateStreamStructure() {
    FrameDependencyStructure stream_structure;
    stream_structure.num_decode_targets = 1;
    stream_structure.templates = {
        FrameDependencyTemplate().Dtis("S"),
        FrameDependencyTemplate().Dtis("S").FrameDiffs({1}),
    };
    return stream_structure;
  }

  void InjectPacketWith(const FrameDependencyStructure& stream_structure,
                        const DependencyDescriptor& dependency_descriptor) {
    InjectPacketWith(stream_structure, dependency_descriptor,
                     ++rtp_sequence_number_);
  }

  void InjectPacketWith(const FrameDependencyStructure& stream_structure,
                        const DependencyDescriptor& dependency_descriptor,
                        uint16_t sequence_number) {
    const std::vector<uint8_t> data = {0, 1, 2, 3, 4};
    RtpPacketReceived rtp_packet(&extension_map_);
    ASSERT_TRUE(rtp_packet.SetExtension<RtpDependencyDescriptorExtension>(
        stream_structure, dependency_descriptor));
    rtp_packet.SetPayload(data);
    mock_on_complete_frame_callback_.ClearExpectedBitstream();
    mock_on_complete_frame_callback_.AppendExpectedBitstream(data);
    rtp_packet.SetMarker(true);
    rtp_packet.SetPayloadType(payload_type_);
    rtp_packet.SetSequenceNumber(sequence_number);
    rtp_video_stream_receiver_->OnRtpPacket(rtp_packet);
  }

 private:
  const int payload_type_ = 123;
  RtpHeaderExtensionMap extension_map_;
  uint16_t rtp_sequence_number_ = 321;
};

TEST_F(RtpVideoStreamReceiver2DependencyDescriptorTest,
       DuplicatePacketBeforeFrameDecodedWithLossNotificationEnabled) {
  config_.rtp.lntf.enabled = true;
  rtp_video_stream_receiver_->SetLossNotificationEnabled(true);
  FrameDependencyStructure stream_structure = CreateStreamStructure();

  DependencyDescriptor keyframe_descriptor;
  keyframe_descriptor.attached_structure =
      std::make_unique<FrameDependencyStructure>(stream_structure);
  keyframe_descriptor.frame_dependencies = stream_structure.templates[0];
  keyframe_descriptor.frame_number = 1;

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame).Times(2);
  InjectPacketWith(stream_structure, keyframe_descriptor,
                   /*sequence_number=*/100);
  // Re-deliver the exact same RTP packet before FrameDecoded() clears
  // PacketBuffer. PacketBuffer re-assembles the frame with the same frame ID.
  InjectPacketWith(stream_structure, keyframe_descriptor,
                   /*sequence_number=*/100);
}

TEST_F(RtpVideoStreamReceiver2DependencyDescriptorTest,
       RepeatedFrameNumberAcrossDistinctPacketsWithLossNotificationEnabled) {
  config_.rtp.lntf.enabled = true;
  rtp_video_stream_receiver_->SetLossNotificationEnabled(true);
  FrameDependencyStructure stream_structure = CreateStreamStructure();

  DependencyDescriptor keyframe_descriptor;
  keyframe_descriptor.attached_structure =
      std::make_unique<FrameDependencyStructure>(stream_structure);
  keyframe_descriptor.frame_dependencies = stream_structure.templates[0];
  keyframe_descriptor.frame_number = 1;

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame).Times(3);
  InjectPacketWith(stream_structure, keyframe_descriptor,
                   /*sequence_number=*/100);
  // Distinct RTP sequence number reusing the same frame_number.
  InjectPacketWith(stream_structure, keyframe_descriptor,
                   /*sequence_number=*/101);

  // Inject a delta frame after a sequence number gap and verify that LNTF
  // preserves the first assembled frame's sequence number (100).
  DependencyDescriptor delta_descriptor;
  delta_descriptor.frame_dependencies = stream_structure.templates[1];
  delta_descriptor.frame_number = 2;
  InjectPacketWith(stream_structure, delta_descriptor,
                   /*sequence_number=*/105);

  EXPECT_EQ(rtcp_packet_parser_.loss_notification()->num_packets(), 1);
  EXPECT_EQ(rtcp_packet_parser_.loss_notification()->last_decoded(), 100u);
  EXPECT_EQ(rtcp_packet_parser_.loss_notification()->last_received(), 105u);
  EXPECT_TRUE(rtcp_packet_parser_.loss_notification()->decodability_flag());
}

TEST_F(RtpVideoStreamReceiver2DependencyDescriptorTest, UnwrapsFrameId) {
  FrameDependencyStructure stream_structure = CreateStreamStructure();

  DependencyDescriptor keyframe_descriptor;
  keyframe_descriptor.attached_structure =
      std::make_unique<FrameDependencyStructure>(stream_structure);
  keyframe_descriptor.frame_dependencies = stream_structure.templates[0];
  keyframe_descriptor.frame_number = 0xfff0;
  // DependencyDescriptor doesn't support reordering delta frame before
  // keyframe. Thus feed a key frame first, then test reodered delta frames.
  int64_t first_picture_id;
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce([&](EncodedFrame* frame) { first_picture_id = frame->Id(); });
  InjectPacketWith(stream_structure, keyframe_descriptor);

  DependencyDescriptor deltaframe1_descriptor;
  deltaframe1_descriptor.frame_dependencies = stream_structure.templates[1];
  deltaframe1_descriptor.frame_number = 0xfffe;

  DependencyDescriptor deltaframe2_descriptor;
  deltaframe2_descriptor.frame_dependencies = stream_structure.templates[1];
  deltaframe2_descriptor.frame_number = 0x0002;

  // Parser should unwrap frame ids correctly even if packets were reordered by
  // the network.
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce([&](EncodedFrame* frame) {
        // 0x0002 - 0xfff0
        EXPECT_EQ(frame->Id() - first_picture_id, 18);
      })
      .WillOnce([&](EncodedFrame* frame) {
        // 0xfffe - 0xfff0
        EXPECT_EQ(frame->Id() - first_picture_id, 14);
      });
  InjectPacketWith(stream_structure, deltaframe2_descriptor);
  InjectPacketWith(stream_structure, deltaframe1_descriptor);
}

TEST_F(RtpVideoStreamReceiver2DependencyDescriptorTest,
       DropsLateDeltaFramePacketWithDependencyDescriptorExtension) {
  FrameDependencyStructure stream_structure1 = CreateStreamStructure();
  FrameDependencyStructure stream_structure2 = CreateStreamStructure();
  // Make sure template ids for these two structures do not collide:
  // adjust structure_id (that is also used as template id offset).
  stream_structure1.structure_id = 13;
  stream_structure2.structure_id =
      stream_structure1.structure_id + stream_structure1.templates.size();

  DependencyDescriptor keyframe1_descriptor;
  keyframe1_descriptor.attached_structure =
      std::make_unique<FrameDependencyStructure>(stream_structure1);
  keyframe1_descriptor.frame_dependencies = stream_structure1.templates[0];
  keyframe1_descriptor.frame_number = 1;
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame);
  InjectPacketWith(stream_structure1, keyframe1_descriptor);

  // Pass in 2nd key frame with different structure.
  DependencyDescriptor keyframe2_descriptor;
  keyframe2_descriptor.attached_structure =
      std::make_unique<FrameDependencyStructure>(stream_structure2);
  keyframe2_descriptor.frame_dependencies = stream_structure2.templates[0];
  keyframe2_descriptor.frame_number = 3;
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame);
  InjectPacketWith(stream_structure2, keyframe2_descriptor);

  // Pass in late delta frame that uses structure of the 1st key frame.
  DependencyDescriptor deltaframe_descriptor;
  deltaframe_descriptor.frame_dependencies = stream_structure1.templates[0];
  deltaframe_descriptor.frame_number = 2;
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame).Times(0);
  InjectPacketWith(stream_structure1, deltaframe_descriptor);
}

TEST_F(RtpVideoStreamReceiver2DependencyDescriptorTest,
       DropsLateKeyFramePacketWithDependencyDescriptorExtension) {
  FrameDependencyStructure stream_structure1 = CreateStreamStructure();
  FrameDependencyStructure stream_structure2 = CreateStreamStructure();
  // Make sure template ids for these two structures do not collide:
  // adjust structure_id (that is also used as template id offset).
  stream_structure1.structure_id = 13;
  stream_structure2.structure_id =
      stream_structure1.structure_id + stream_structure1.templates.size();

  DependencyDescriptor keyframe1_descriptor;
  keyframe1_descriptor.attached_structure =
      std::make_unique<FrameDependencyStructure>(stream_structure1);
  keyframe1_descriptor.frame_dependencies = stream_structure1.templates[0];
  keyframe1_descriptor.frame_number = 1;

  DependencyDescriptor keyframe2_descriptor;
  keyframe2_descriptor.attached_structure =
      std::make_unique<FrameDependencyStructure>(stream_structure2);
  keyframe2_descriptor.frame_dependencies = stream_structure2.templates[0];
  keyframe2_descriptor.frame_number = 3;

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_EQ(frame->Id() & 0xFFFF, 3); });
  InjectPacketWith(stream_structure2, keyframe2_descriptor);
  InjectPacketWith(stream_structure1, keyframe1_descriptor);

  // Pass in delta frame that uses structure of the 2nd key frame. Late key
  // frame shouldn't block it.
  DependencyDescriptor deltaframe_descriptor;
  deltaframe_descriptor.frame_dependencies = stream_structure2.templates[0];
  deltaframe_descriptor.frame_number = 4;
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_EQ(frame->Id() & 0xFFFF, 4); });
  InjectPacketWith(stream_structure2, deltaframe_descriptor);
}

TEST_F(RtpVideoStreamReceiver2DependencyDescriptorTest,
       RequestKeyframeIfInitialKeyframePacketIsLost) {
  FrameDependencyStructure stream_structure = CreateStreamStructure();

  DependencyDescriptor keyframe_descriptor_without_structure;
  keyframe_descriptor_without_structure.frame_dependencies =
      stream_structure.templates[0];
  keyframe_descriptor_without_structure.frame_number = 0;

  InjectPacketWith(stream_structure, keyframe_descriptor_without_structure);

  // Not enough time since last keyframe request
  time_controller_.AdvanceTime(TimeDelta::Millis(500));
  InjectPacketWith(stream_structure, keyframe_descriptor_without_structure);
  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(1));

  time_controller_.AdvanceTime(TimeDelta::Millis(501));
  InjectPacketWith(stream_structure, keyframe_descriptor_without_structure);
  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(2));
}

TEST_F(RtpVideoStreamReceiver2DependencyDescriptorTest,
       RetryStashedPacketsAfterReceivingScalabilityStructure) {
  FrameDependencyStructure stream_structure1 = CreateStreamStructure();
  FrameDependencyStructure stream_structure2 = CreateStreamStructure();
  // Make sure template ids for these two structures do not collide:
  // adjust structure_id (that is also used as template id offset).
  stream_structure1.structure_id = 13;
  stream_structure2.structure_id =
      stream_structure1.structure_id + stream_structure1.templates.size();

  DependencyDescriptor keyframe1_descriptor;
  keyframe1_descriptor.attached_structure =
      std::make_unique<FrameDependencyStructure>(stream_structure1);
  keyframe1_descriptor.frame_dependencies = stream_structure1.templates[0];
  keyframe1_descriptor.frame_number = 1;

  DependencyDescriptor keyframe2_descriptor;
  keyframe2_descriptor.attached_structure =
      std::make_unique<FrameDependencyStructure>(stream_structure2);
  keyframe2_descriptor.frame_dependencies = stream_structure2.templates[0];
  keyframe2_descriptor.frame_number = 2;

  DependencyDescriptor deltaframe_descriptor;
  deltaframe_descriptor.frame_dependencies = stream_structure2.templates[1];
  deltaframe_descriptor.frame_number = 3;

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_EQ(frame->Id() & 0xFFFF, 1); })
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_EQ(frame->Id() & 0xFFFF, 2); })
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_EQ(frame->Id() & 0xFFFF, 3); });

  InjectPacketWith(stream_structure1, keyframe1_descriptor);
  InjectPacketWith(stream_structure2, deltaframe_descriptor);
  InjectPacketWith(stream_structure2, keyframe2_descriptor);
}

TEST_F(RtpVideoStreamReceiver2DependencyDescriptorTest,
       RetryStashedPacketsAfterReceivingEarlierScalabilityStructure) {
  FrameDependencyStructure stream_structure1 = CreateStreamStructure();
  FrameDependencyStructure stream_structure2 = CreateStreamStructure();
  FrameDependencyStructure stream_structure3 = CreateStreamStructure();
  // Make sure template ids for these two structures do not collide:
  // adjust structure_id (that is also used as template id offset).
  stream_structure1.structure_id = 13;
  stream_structure2.structure_id =
      stream_structure1.structure_id + stream_structure1.templates.size();
  stream_structure3.structure_id =
      stream_structure2.structure_id + stream_structure2.templates.size();

  DependencyDescriptor keyframe1_descriptor;
  keyframe1_descriptor.attached_structure =
      std::make_unique<FrameDependencyStructure>(stream_structure1);
  keyframe1_descriptor.frame_dependencies = stream_structure1.templates[0];
  keyframe1_descriptor.frame_number = 1;

  DependencyDescriptor keyframe2_descriptor;
  keyframe2_descriptor.attached_structure =
      std::make_unique<FrameDependencyStructure>(stream_structure2);
  keyframe2_descriptor.frame_dependencies = stream_structure2.templates[0];
  keyframe2_descriptor.frame_number = 2;

  DependencyDescriptor deltaframe2_descriptor;
  deltaframe2_descriptor.frame_dependencies = stream_structure2.templates[1];
  deltaframe2_descriptor.frame_number = 3;

  DependencyDescriptor keyframe3_descriptor;
  keyframe3_descriptor.attached_structure =
      std::make_unique<FrameDependencyStructure>(stream_structure3);
  keyframe3_descriptor.frame_dependencies = stream_structure3.templates[0];
  keyframe3_descriptor.frame_number = 4;

  DependencyDescriptor deltaframe3_descriptor;
  deltaframe3_descriptor.frame_dependencies = stream_structure3.templates[1];
  deltaframe3_descriptor.frame_number = 5;

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_EQ(frame->Id() & 0xFFFF, 1); })
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_EQ(frame->Id() & 0xFFFF, 2); })
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_EQ(frame->Id() & 0xFFFF, 3); })
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_EQ(frame->Id() & 0xFFFF, 4); })
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_EQ(frame->Id() & 0xFFFF, 5); });

  InjectPacketWith(stream_structure1, keyframe1_descriptor);
  InjectPacketWith(stream_structure2, deltaframe2_descriptor);
  InjectPacketWith(stream_structure3, deltaframe3_descriptor);
  InjectPacketWith(stream_structure2, keyframe2_descriptor);
  InjectPacketWith(stream_structure3, keyframe3_descriptor);
}

TEST_F(RtpVideoStreamReceiver2Test, TransformFrame) {
  scoped_refptr<MockFrameTransformer> mock_frame_transformer =
      make_ref_counted<testing::NiceMock<MockFrameTransformer>>();
  EXPECT_CALL(*mock_frame_transformer,
              RegisterTransformedFrameSinkCallback(_, config_.rtp.remote_ssrc));
  auto receiver = std::make_unique<RtpVideoStreamReceiver2>(
      env_, TaskQueueBase::Current(), &mock_transport_, nullptr, nullptr,
      &config_, kMaxWaitForKeyframe, rtp_receive_statistics_.get(), nullptr,
      nullptr, &nack_periodic_processor_, &mock_on_complete_frame_callback_,
      nullptr, mock_frame_transformer, nullptr);
  receiver->AddReceiveCodec(kPayloadType, kVideoCodecGeneric, {},
                            /*raw_payload=*/false);

  RtpPacketReceived rtp_packet;
  rtp_packet.SetPayloadType(kPayloadType);
  CopyOnWriteBuffer data("1234");
  rtp_packet.SetSequenceNumber(1);
  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameKey);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);
  std::unique_ptr<TransformableFrameInterface> transformed_frame;
  EXPECT_CALL(*mock_frame_transformer, Transform(_))
      .WillOnce(testing::SaveArgByMove<0>(&transformed_frame));
  receiver->OnReceivedPayloadDataForTesting(data, rtp_packet, video_header);
  EXPECT_TRUE(transformed_frame->ReceiveTime().has_value());
  EXPECT_FALSE(transformed_frame->CaptureTime());
  EXPECT_FALSE(transformed_frame->SenderCaptureTimeOffset());

  EXPECT_CALL(*mock_frame_transformer,
              UnregisterTransformedFrameSinkCallback(config_.rtp.remote_ssrc));
  receiver = nullptr;
}

TEST_F(RtpVideoStreamReceiver2Test, TransformFrameWithAbsoluteCaptureTime) {
  scoped_refptr<MockFrameTransformer> mock_frame_transformer =
      make_ref_counted<testing::NiceMock<MockFrameTransformer>>();
  EXPECT_CALL(*mock_frame_transformer,
              RegisterTransformedFrameSinkCallback(_, config_.rtp.remote_ssrc));
  auto receiver = std::make_unique<RtpVideoStreamReceiver2>(
      env_, TaskQueueBase::Current(), &mock_transport_, nullptr, nullptr,
      &config_, kMaxWaitForKeyframe, rtp_receive_statistics_.get(), nullptr,
      nullptr, &nack_periodic_processor_, &mock_on_complete_frame_callback_,
      nullptr, mock_frame_transformer, nullptr);
  receiver->AddReceiveCodec(kPayloadType, kVideoCodecGeneric, {},
                            /*raw_payload=*/false);

  constexpr RtpHeaderExtensionId kId0(1);
  RtpHeaderExtensionMap extension_map;
  extension_map.Register<AbsoluteCaptureTimeExtension>(kId0);
  RtpPacketReceived rtp_packet(&extension_map);
  rtp_packet.SetPayloadType(kPayloadType);
  CopyOnWriteBuffer data("1234");
  rtp_packet.SetSequenceNumber(1);

  Timestamp capture_time = Timestamp::Millis(1234);
  TimeDelta sender_capture_time_offset = TimeDelta::Millis(56);
  AbsoluteCaptureTime absolute_capture_time = {
      .absolute_capture_timestamp = Int64MsToUQ32x32(capture_time.ms()),
      .estimated_capture_clock_offset =
          Int64MsToUQ32x32(sender_capture_time_offset.ms())};
  rtp_packet.SetExtension<AbsoluteCaptureTimeExtension>(absolute_capture_time);

  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameKey);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data);

  std::unique_ptr<TransformableFrameInterface> transformed_frame;
  EXPECT_CALL(*mock_frame_transformer, Transform(_))
      .WillOnce(testing::SaveArgByMove<0>(&transformed_frame));
  receiver->OnReceivedPayloadDataForTesting(data, rtp_packet, video_header);
  EXPECT_TRUE(transformed_frame->ReceiveTime().has_value());
  EXPECT_EQ(transformed_frame->CaptureTime(), capture_time);
  EXPECT_EQ(transformed_frame->SenderCaptureTimeOffset(),
            sender_capture_time_offset);

  EXPECT_CALL(*mock_frame_transformer,
              UnregisterTransformedFrameSinkCallback(config_.rtp.remote_ssrc));
  receiver = nullptr;
}

TEST_F(RtpVideoStreamReceiver2Test,
       ReplaceFrameTransformerResetsPreviousDelegateBeforeDestruction) {
  auto transformer1 =
      make_ref_counted<testing::NiceMock<MockFrameTransformer>>();
  auto transformer2 =
      make_ref_counted<testing::NiceMock<MockFrameTransformer>>();

  scoped_refptr<TransformedFrameCallback> callback1;
  EXPECT_CALL(*transformer1,
              RegisterTransformedFrameSinkCallback(_, config_.rtp.remote_ssrc))
      .WillOnce(testing::SaveArg<0>(&callback1));

  auto receiver = std::make_unique<RtpVideoStreamReceiver2>(
      env_, TaskQueueBase::Current(), &mock_transport_, nullptr, nullptr,
      &config_, kMaxWaitForKeyframe, rtp_receive_statistics_.get(), nullptr,
      nullptr, &nack_periodic_processor_, &mock_on_complete_frame_callback_,
      nullptr, transformer1, nullptr);
  receiver->AddReceiveCodec(kPayloadType, kVideoCodecGeneric, {},
                            /*raw_payload=*/false);
  ASSERT_NE(callback1, nullptr);

  RtpPacketReceived rtp_packet;
  rtp_packet.SetPayloadType(kPayloadType);
  rtp_packet.SetSequenceNumber(1);
  CopyOnWriteBuffer data("1234");
  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameKey);

  std::unique_ptr<TransformableFrameInterface> transformed_frame;
  EXPECT_CALL(*transformer1, Transform(_))
      .WillOnce(testing::SaveArgByMove<0>(&transformed_frame));
  receiver->OnReceivedPayloadDataForTesting(data, rtp_packet, video_header);
  ASSERT_NE(transformed_frame, nullptr);

  // Queue a ManageFrame task on the retired delegate before replacing the
  // transformer and destroying the receiver.
  callback1->OnTransformedFrame(std::move(transformed_frame));

  {
    testing::InSequence seq;
    EXPECT_CALL(*transformer1, UnregisterTransformedFrameSinkCallback(
                                   config_.rtp.remote_ssrc));
    EXPECT_CALL(*transformer2, RegisterTransformedFrameSinkCallback(
                                   _, config_.rtp.remote_ssrc));
    EXPECT_CALL(*transformer2, UnregisterTransformedFrameSinkCallback(
                                   config_.rtp.remote_ssrc));
  }
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame).Times(0);

  receiver->SetDepacketizerToDecoderFrameTransformer(transformer2);
  receiver = nullptr;

  // Flush the queued ManageFrame task from `callback1`; the retired delegate
  // must have cleared its raw `receiver_` pointer during replacement.
  time_controller_.AdvanceTime(TimeDelta::Zero());
}

TEST_F(RtpVideoStreamReceiver2Test,
       SetNullFrameTransformerResetsExistingDelegate) {
  auto transformer =
      make_ref_counted<testing::NiceMock<MockFrameTransformer>>();

  scoped_refptr<TransformedFrameCallback> callback;
  EXPECT_CALL(*transformer,
              RegisterTransformedFrameSinkCallback(_, config_.rtp.remote_ssrc))
      .WillOnce(testing::SaveArg<0>(&callback));

  auto receiver = std::make_unique<RtpVideoStreamReceiver2>(
      env_, TaskQueueBase::Current(), &mock_transport_, nullptr, nullptr,
      &config_, kMaxWaitForKeyframe, rtp_receive_statistics_.get(), nullptr,
      nullptr, &nack_periodic_processor_, &mock_on_complete_frame_callback_,
      nullptr, transformer, nullptr);
  receiver->AddReceiveCodec(kPayloadType, kVideoCodecGeneric, {},
                            /*raw_payload=*/false);
  ASSERT_NE(callback, nullptr);

  RtpPacketReceived packet1;
  packet1.SetPayloadType(kPayloadType);
  packet1.SetSequenceNumber(1);
  CopyOnWriteBuffer data1("1234");
  RTPVideoHeader keyframe_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameKey);

  std::unique_ptr<TransformableFrameInterface> transformed_frame;
  EXPECT_CALL(*transformer, Transform(_))
      .WillOnce(testing::SaveArgByMove<0>(&transformed_frame));
  receiver->OnReceivedPayloadDataForTesting(data1, packet1, keyframe_header);
  ASSERT_NE(transformed_frame, nullptr);

  EXPECT_CALL(*transformer,
              UnregisterTransformedFrameSinkCallback(config_.rtp.remote_ssrc));
  receiver->SetDepacketizerToDecoderFrameTransformer(nullptr);

  // Late frame from the cleared transformer's callback must be ignored.
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame).Times(0);
  callback->OnTransformedFrame(std::move(transformed_frame));
  time_controller_.AdvanceTime(TimeDelta::Zero());
  testing::Mock::VerifyAndClearExpectations(&mock_on_complete_frame_callback_);

  // Subsequent frames bypass transformation and reach OnCompleteFrame directly.
  EXPECT_CALL(*transformer, Transform(_)).Times(0);
  EXPECT_CALL(*transformer, UnregisterTransformedFrameSinkCallback(_)).Times(0);
  CopyOnWriteBuffer data2("5678");
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data2);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame);

  RtpPacketReceived packet2;
  packet2.SetPayloadType(kPayloadType);
  packet2.SetSequenceNumber(2);
  packet2.SetTimestamp(3000);
  receiver->OnReceivedPayloadDataForTesting(data2, packet2, keyframe_header);

  receiver = nullptr;
}

// Test default behavior and when playout delay is overridden by field trial.
VideoPlayoutDelay TransmittedPlayoutDelay() {
  return {TimeDelta::Millis(100), TimeDelta::Millis(200)};
}
VideoPlayoutDelay ForcedPlayoutDelay() {
  return {TimeDelta::Millis(70), TimeDelta::Millis(90)};
}
struct PlayoutDelayOptions {
  std::string field_trial;
  VideoPlayoutDelay expected_delay;
};
PlayoutDelayOptions DefaultBehavior() {
  return {.field_trial = "", .expected_delay = TransmittedPlayoutDelay()};
}
PlayoutDelayOptions OverridePlayoutDelay() {
  return {.field_trial = "WebRTC-ForcePlayoutDelay/min_ms:70,max_ms:90/",
          .expected_delay = ForcedPlayoutDelay()};
}

class RtpVideoStreamReceiver2TestPlayoutDelay
    : public RtpVideoStreamReceiver2Test,
      public ::testing::WithParamInterface<PlayoutDelayOptions> {
 protected:
  RtpVideoStreamReceiver2TestPlayoutDelay()
      : RtpVideoStreamReceiver2Test(GetParam().field_trial) {}
};

INSTANTIATE_TEST_SUITE_P(PlayoutDelay,
                         RtpVideoStreamReceiver2TestPlayoutDelay,
                         Values(DefaultBehavior(), OverridePlayoutDelay()));

TEST_P(RtpVideoStreamReceiver2TestPlayoutDelay, PlayoutDelay) {
  CopyOnWriteBuffer payload_data("1234");
  RtpHeaderExtensionMap extension_map;
  extension_map.Register<PlayoutDelayLimits>(RtpHeaderExtensionId(1));
  RtpPacketToSend packet_to_send(&extension_map);
  packet_to_send.SetPayloadType(kPayloadType);
  packet_to_send.SetSequenceNumber(1);

  // Set playout delay on outgoing packet.
  EXPECT_TRUE(packet_to_send.SetExtension<PlayoutDelayLimits>(
      TransmittedPlayoutDelay()));
  packet_to_send.SetPayload(payload_data);

  RtpPacketReceived received_packet(&extension_map);
  received_packet.Parse(packet_to_send.data(), packet_to_send.size());

  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameKey);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(payload_data);
  // Expect the playout delay of encoded frame to be the same as the transmitted
  // playout delay unless it was overridden by a field trial.
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_))
      .WillOnce([expected_playout_delay =
                     GetParam().expected_delay](EncodedFrame* frame) {
        EXPECT_EQ(frame->EncodedImage().PlayoutDelay(), expected_playout_delay);
      });
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      received_packet.PayloadBuffer(), received_packet, video_header);
}

#ifdef RTC_ENABLE_H265
RTPVideoHeader GetDefaultH265VideoHeader() {
  RTPVideoHeader video_header;
  video_header.codec = kVideoCodecH265;
  return video_header;
}

class RtpVideoStreamReceiver2TestH265 : public RtpVideoStreamReceiver2Test {
 protected:
  RtpVideoStreamReceiver2TestH265() : RtpVideoStreamReceiver2Test("") {}
};

TEST_F(RtpVideoStreamReceiver2TestH265, H265Bitstream) {
  constexpr int kH265PayloadType = 98;
  CodecParameterMap codec_params;
  rtp_video_stream_receiver_->AddReceiveCodec(kH265PayloadType, kVideoCodecH265,
                                              codec_params,
                                              /*raw_payload=*/false);
  rtp_video_stream_receiver_->StartReceive();

  // Data is generated by WebCodecs H265 encoder, with 720p fake media capturer.
  // IDR is not complete.
  constexpr uint8_t vps[] = {0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0c, 0x01,
                             0xff, 0xff, 0x21, 0x40, 0x00, 0x00, 0x03, 0x00,
                             0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00,
                             0x7b, 0x3c, 0x0c, 0x00, 0x00, 0x03, 0x00, 0x04,
                             0x00, 0x00, 0x03, 0x00, 0x79, 0x40};
  constexpr uint8_t sps[] = {
      0x00, 0x00, 0x00, 0x01, 0x42, 0x01, 0x01, 0x21, 0x40, 0x00, 0x00, 0x03,
      0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x7b, 0xa0, 0x02,
      0x80, 0x80, 0x2d, 0x16, 0x8f, 0x92, 0x46, 0xd9, 0x3f, 0xf6, 0x02, 0x80,
      0x10, 0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00, 0x03, 0x01, 0xe6, 0x45,
      0xde, 0xf7, 0xe0, 0x04, 0x65, 0x00, 0x23, 0x28, 0x80};
  constexpr uint8_t pps[] = {0x00, 0x00, 0x00, 0x01, 0x44, 0x01,
                             0xc0, 0x25, 0x64, 0xc0, 0xed, 0x90};
  constexpr uint8_t idr[] = {0x00, 0x00, 0x00, 0x01, 0x26, 0x01, 0xaf,
                             0xb0, 0x87, 0x11, 0x7a, 0xc1, 0x45, 0x57,
                             0x3f, 0xff, 0x57, 0x14, 0x5f, 0xf7, 0x7a,
                             0x37, 0xfd, 0xe3, 0xd9};

  RtpPacketReceived rtp_packet;
  rtp_packet.SetPayloadType(kPayloadType);
  rtp_packet.SetSequenceNumber(0);
  rtp_packet.SetPayloadType(kH265PayloadType);
  RTPVideoHeader video_header = GetDefaultH265VideoHeader();
  mock_on_complete_frame_callback_.AppendExpectedBitstream(vps);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      CopyOnWriteBuffer(vps), rtp_packet, video_header);

  rtp_packet.SetSequenceNumber(1);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(sps);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      CopyOnWriteBuffer(sps), rtp_packet, video_header);

  rtp_packet.SetSequenceNumber(2);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(pps);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      CopyOnWriteBuffer(pps), rtp_packet, video_header);

  rtp_packet.SetSequenceNumber(3);
  rtp_packet.SetMarker(true);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(idr);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      CopyOnWriteBuffer(idr), rtp_packet, video_header);
}

TEST_F(RtpVideoStreamReceiver2Test, AddReceiveCodecWhileReceivingH265) {
  // Start receiving before adding any H26x codecs.
  rtp_video_stream_receiver_->StartReceive();

  constexpr int kH265PayloadType = 98;
  constexpr uint8_t vps[] = {0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0c, 0x01,
                             0xff, 0xff, 0x21, 0x40, 0x00, 0x00, 0x03, 0x00,
                             0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00,
                             0x7b, 0x3c, 0x0c, 0x00, 0x00, 0x03, 0x00, 0x04,
                             0x00, 0x00, 0x03, 0x00, 0x79, 0x40};

  RtpPacketReceived rtp_packet;
  rtp_packet.SetSequenceNumber(0);
  rtp_packet.SetPayloadType(kH265PayloadType);
  RTPVideoHeader video_header = GetDefaultH265VideoHeader();

  // Before adding H.265 receive codec, packet insertion does not trigger
  // complete frame.
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame).Times(0);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      CopyOnWriteBuffer(vps), rtp_packet, video_header);

  // Add H.265 receive codec while receiving.
  CodecParameterMap codec_params;
  rtp_video_stream_receiver_->AddReceiveCodec(kH265PayloadType, kVideoCodecH265,
                                              codec_params,
                                              /*raw_payload=*/false);

  // Now h26x_packet_buffer_ is initialized, so packet insertion succeeds and
  // frame callback is invoked.
  mock_on_complete_frame_callback_.AppendExpectedBitstream(vps);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      CopyOnWriteBuffer(vps), rtp_packet, video_header);
  mock_on_complete_frame_callback_.ClearExpectedBitstream();
}
#endif  // RTC_ENABLE_H265

TEST_F(RtpVideoStreamReceiver2Test, LogsReceivedPacketToEventLog) {
  RtpPacketReceived rtp_packet;

  EXPECT_CALL(log_, LogProxy(_));
  rtp_video_stream_receiver_->OnRtpPacket(rtp_packet);
}

TEST_F(RtpVideoStreamReceiver2Test, DoesNotLogRecoveredPacketToEventLog) {
  RtpPacketReceived recovered_packet;
  recovered_packet.set_recovered(true);

  EXPECT_CALL(log_, LogProxy(_)).Times(0);
  rtp_video_stream_receiver_->OnRtpPacket(recovered_packet);
}

TEST_F(RtpVideoStreamReceiver2Test, PrunesHistoryMapsOnUndecodableFrames) {
  const std::vector<uint8_t> data = {0, 1, 2, 3, 4};
  rtp_video_stream_receiver_->StartReceive();

  RtpHeaderExtensionMap extension_map;
  extension_map.Register<RtpGenericFrameDescriptorExtension00>(
      RtpHeaderExtensionId(5));

  // Inject 2049 complete single-packet frames.
  // Each frame completes and inserts an entry into last_seq_num_for_pic_id_
  // and last_timestamp_for_pic_id_.
  // After 2048 frames (kMaxFrameHistorySize), the capacity limit is reached,
  // and oldest entries are evicted as new ones arrive.
  for (int i = 1; i <= 2049; ++i) {
    mock_on_complete_frame_callback_.ClearExpectedBitstream();
    mock_on_complete_frame_callback_.AppendExpectedBitstream(
        std::span(data).subspan(1));

    RtpPacketReceived rtp_packet(&extension_map);
    rtp_packet.SetPayloadType(kPayloadType);

    RtpGenericFrameDescriptor generic_descriptor;
    generic_descriptor.SetFirstPacketInSubFrame(true);
    generic_descriptor.SetLastPacketInSubFrame(true);
    generic_descriptor.SetFrameId(i);
    ASSERT_TRUE(rtp_packet.SetExtension<RtpGenericFrameDescriptorExtension00>(
        generic_descriptor));

    rtp_packet.SetPayload(data);
    rtp_packet.SetMarker(true);
    rtp_packet.SetSequenceNumber(static_cast<uint16_t>(i));
    rtp_packet.SetTimestamp(static_cast<uint32_t>(90000 + i * 3000));
    rtp_packet.SetSsrc(kSsrc);

    rtp_video_stream_receiver_->OnRtpPacket(rtp_packet);
  }

  // Verify that history maps are bounded by kMaxFrameHistorySize (2048).
  ASSERT_EQ(rtp_video_stream_receiver_->last_seq_num_for_pic_id().size(),
            2048u);
  ASSERT_EQ(rtp_video_stream_receiver_->last_timestamp_for_pic_id().size(),
            2048u);

  // Verify that the oldest frame (1) was evicted, while newer frames (2..2049)
  // are retained.
  EXPECT_FALSE(
      rtp_video_stream_receiver_->last_seq_num_for_pic_id().contains(1));
  EXPECT_FALSE(
      rtp_video_stream_receiver_->last_timestamp_for_pic_id().contains(1));
  EXPECT_EQ(
      rtp_video_stream_receiver_->last_seq_num_for_pic_id().begin()->first, 2);
  EXPECT_EQ(
      rtp_video_stream_receiver_->last_seq_num_for_pic_id().rbegin()->first,
      2049);

  // Calling FrameDecoded for the newest frame clears all remaining entries.
  rtp_video_stream_receiver_->FrameDecoded(2049);
  EXPECT_THAT(rtp_video_stream_receiver_->last_seq_num_for_pic_id(), IsEmpty());
  EXPECT_THAT(rtp_video_stream_receiver_->last_timestamp_for_pic_id(),
              IsEmpty());
}

TEST_F(RtpVideoStreamReceiver2Test, RemoveReceiveCodecsResetsState) {
  constexpr uint8_t kH264PayloadType = 99;

  // Configure H.264 with SPS/PPS/IDR forced keyframe mode.
  CodecParameterMap codec_params_keyframe;
  codec_params_keyframe.emplace(kH264FmtpSpsPpsIdrInKeyframe, "");
  rtp_video_stream_receiver_->AddReceiveCodec(kH264PayloadType, kVideoCodecH264,
                                              codec_params_keyframe,
                                              /*raw_payload=*/false);

  // Remove codecs. This must reset sps_pps_idr_is_h264_keyframe_ to false.
  rtp_video_stream_receiver_->RemoveReceiveCodecs();

  // Re-add H.264 without forcing SPS/PPS/IDR keyframe mode.
  CodecParameterMap empty_codec_params;
  rtp_video_stream_receiver_->AddReceiveCodec(kH264PayloadType, kVideoCodecH264,
                                              empty_codec_params,
                                              /*raw_payload=*/false);
  rtp_video_stream_receiver_->StartReceive();

  // Send SPS
  CopyOnWriteBuffer sps_data;
  RtpPacketReceived rtp_packet;
  RTPVideoHeader sps_video_header = GetDefaultH264VideoHeader();
  AddSps(&sps_video_header, 0, &sps_data);
  rtp_packet.SetSequenceNumber(0);
  rtp_packet.SetPayloadType(kH264PayloadType);
  sps_video_header.is_first_packet_in_frame = true;
  sps_video_header.frame_type = VideoFrameType::kEmptyFrame;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(sps_data);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      sps_data, rtp_packet, sps_video_header);

  // Send PPS
  CopyOnWriteBuffer pps_data;
  RTPVideoHeader pps_video_header = GetDefaultH264VideoHeader();
  AddPps(&pps_video_header, 0, 1, &pps_data);
  rtp_packet.SetSequenceNumber(1);
  rtp_packet.SetPayloadType(kH264PayloadType);
  pps_video_header.is_first_packet_in_frame = true;
  pps_video_header.frame_type = VideoFrameType::kEmptyFrame;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(pps_data);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      pps_data, rtp_packet, pps_video_header);

  // Send IDR packet
  CopyOnWriteBuffer idr_data;
  RTPVideoHeader idr_video_header = GetDefaultH264VideoHeader();
  AddIdr(&idr_video_header, 1);
  rtp_packet.SetSequenceNumber(2);
  rtp_packet.SetPayloadType(kH264PayloadType);
  rtp_packet.SetMarker(true);
  idr_video_header.is_first_packet_in_frame = true;
  idr_video_header.is_last_packet_in_frame = true;
  idr_video_header.frame_type = VideoFrameType::kVideoFrameKey;

  const uint8_t idr[] = {0x65, 1, 2, 3};
  idr_data.AppendData(idr);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(idr_data);
  // Because sps_pps_idr_is_h264_keyframe_ was reset to false, IDR-only
  // keyframes are allowed again. Therefore, the IDR frame completes as a
  // keyframe.
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_TRUE(frame->is_keyframe()); });
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      idr_data, rtp_packet, idr_video_header);
  mock_on_complete_frame_callback_.ClearExpectedBitstream();
}

TEST_F(RtpVideoStreamReceiver2Test, SetReceiveCodecsUpdatesInPlace) {
  constexpr uint8_t kH264Pt = 99;
  constexpr uint8_t kVp8Pt = 100;

  RtpVideoStreamReceiver2::ReceiveCodec h264_codec{
      .payload_type = kH264Pt,
      .video_codec = kVideoCodecH264,
  };
  RtpVideoStreamReceiver2::ReceiveCodec vp8_codec{
      .payload_type = kVp8Pt,
      .video_codec = kVideoCodecVP8,
  };

  rtp_video_stream_receiver_->SetReceiveCodecs({h264_codec});
  rtp_video_stream_receiver_->StartReceive();

  // Update codecs in-place by adding VP8 while keeping H264.
  rtp_video_stream_receiver_->SetReceiveCodecs({h264_codec, vp8_codec});

  // Verify VP8 payload is accepted and produces a complete frame.
  const uint8_t vp8_payload[] = {1, 2, 3, 4};
  CopyOnWriteBuffer payload(vp8_payload);
  RtpPacketReceived rtp_packet;
  rtp_packet.SetPayloadType(kVp8Pt);
  rtp_packet.SetSequenceNumber(1);

  RTPVideoHeader video_header;
  video_header.codec = kVideoCodecVP8;
  video_header.video_type_header.emplace<RTPVideoHeaderVP8>();
  video_header.is_first_packet_in_frame = true;
  video_header.is_last_packet_in_frame = true;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;

  mock_on_complete_frame_callback_.AppendExpectedBitstream(payload);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_TRUE(frame->is_keyframe()); });
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      payload, rtp_packet, video_header);
  mock_on_complete_frame_callback_.ClearExpectedBitstream();
}

TEST_F(RtpVideoStreamReceiver2Test,
       SetReceiveCodecsPreservesPacketBufferAndFrameAssembly) {
  constexpr uint8_t kH264Pt = 99;
  constexpr uint8_t kVp8Pt = 100;

  RtpVideoStreamReceiver2::ReceiveCodec h264_codec{
      .payload_type = kH264Pt,
      .video_codec = kVideoCodecH264,
  };
  RtpVideoStreamReceiver2::ReceiveCodec vp8_codec{
      .payload_type = kVp8Pt,
      .video_codec = kVideoCodecVP8,
  };

  rtp_video_stream_receiver_->SetReceiveCodecs({h264_codec});
  rtp_video_stream_receiver_->StartReceive();

  // Send SPS packet of H.264 frame.
  CopyOnWriteBuffer sps_data;
  RtpPacketReceived rtp_packet;
  RTPVideoHeader sps_video_header = GetDefaultH264VideoHeader();
  AddSps(&sps_video_header, 0, &sps_data);
  rtp_packet.SetSequenceNumber(0);
  rtp_packet.SetPayloadType(kH264Pt);
  sps_video_header.is_first_packet_in_frame = true;
  sps_video_header.frame_type = VideoFrameType::kEmptyFrame;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(sps_data);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      sps_data, rtp_packet, sps_video_header);

  // Send PPS packet of H.264 frame.
  CopyOnWriteBuffer pps_data;
  RTPVideoHeader pps_video_header = GetDefaultH264VideoHeader();
  AddPps(&pps_video_header, 0, 1, &pps_data);
  rtp_packet.SetSequenceNumber(1);
  rtp_packet.SetPayloadType(kH264Pt);
  pps_video_header.is_first_packet_in_frame = true;
  pps_video_header.frame_type = VideoFrameType::kEmptyFrame;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(pps_data);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      pps_data, rtp_packet, pps_video_header);

  // Update receive codecs in-place mid-frame assembly (adding VP8).
  rtp_video_stream_receiver_->SetReceiveCodecs({h264_codec, vp8_codec});

  // Send final IDR packet. Packet buffer must be preserved so that the complete
  // frame (SPS + PPS + IDR) is assembled and delivered.
  CopyOnWriteBuffer idr_data;
  RTPVideoHeader idr_video_header = GetDefaultH264VideoHeader();
  AddIdr(&idr_video_header, 1);
  rtp_packet.SetSequenceNumber(2);
  rtp_packet.SetPayloadType(kH264Pt);
  rtp_packet.SetMarker(true);
  idr_video_header.is_first_packet_in_frame = true;
  idr_video_header.is_last_packet_in_frame = true;
  idr_video_header.frame_type = VideoFrameType::kVideoFrameKey;

  const uint8_t idr[] = {0x65, 1, 2, 3};
  idr_data.AppendData(idr);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(idr_data);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_TRUE(frame->is_keyframe()); });
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      idr_data, rtp_packet, idr_video_header);
  mock_on_complete_frame_callback_.ClearExpectedBitstream();
}

TEST_F(RtpVideoStreamReceiver2Test,
       SetReceiveCodecsSelectivelyRemovesAndPreservesPayloadTypes) {
  constexpr uint8_t kH264Pt = 99;
  constexpr uint8_t kVp8Pt = 100;
  constexpr uint8_t kVp9Pt = 101;

  RtpVideoStreamReceiver2::ReceiveCodec h264_codec{
      .payload_type = kH264Pt,
      .video_codec = kVideoCodecH264,
  };
  RtpVideoStreamReceiver2::ReceiveCodec vp8_codec{
      .payload_type = kVp8Pt,
      .video_codec = kVideoCodecVP8,
  };
  RtpVideoStreamReceiver2::ReceiveCodec vp9_codec{
      .payload_type = kVp9Pt,
      .video_codec = kVideoCodecVP9,
  };

  // Initially configure H264, VP8, and VP9.
  rtp_video_stream_receiver_->SetReceiveCodecs(
      {h264_codec, vp8_codec, vp9_codec});
  rtp_video_stream_receiver_->StartReceive();

  // Reconfigure codecs to keep H264 and VP8, but remove VP9.
  rtp_video_stream_receiver_->SetReceiveCodecs({h264_codec, vp8_codec});

  const uint8_t dummy_payload[] = {1, 2, 3, 4};
  CopyOnWriteBuffer payload(dummy_payload);

  // 1. Packet for retained VP8 (PT 100) completes a frame.
  RtpPacketReceived vp8_packet;
  vp8_packet.SetPayloadType(kVp8Pt);
  vp8_packet.SetSequenceNumber(1);
  RTPVideoHeader vp8_header;
  vp8_header.codec = kVideoCodecVP8;
  vp8_header.video_type_header.emplace<RTPVideoHeaderVP8>();
  vp8_header.is_first_packet_in_frame = true;
  vp8_header.is_last_packet_in_frame = true;
  vp8_header.frame_type = VideoFrameType::kVideoFrameKey;

  mock_on_complete_frame_callback_.AppendExpectedBitstream(payload);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame).Times(1);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      payload, vp8_packet, vp8_header);
  mock_on_complete_frame_callback_.ClearExpectedBitstream();

  // 2. Packet for removed VP9 (PT 101) is dropped by OnRtpPacket.
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame).Times(0);
  RtpPacketReceived vp9_packet;
  vp9_packet.SetPayloadType(kVp9Pt);
  vp9_packet.SetSequenceNumber(2);
  rtp_video_stream_receiver_->OnRtpPacket(vp9_packet);
}

TEST_F(RtpVideoStreamReceiver2Test,
       BufferedFrameForRemovedCodecDoesNotCrashOnAssembly) {
  constexpr uint8_t kAv1Pt = 100;
  RtpVideoStreamReceiver2::ReceiveCodec av1_codec{
      .payload_type = kAv1Pt,
      .video_codec = kVideoCodecAV1,
  };

  rtp_video_stream_receiver_->SetReceiveCodecs({av1_codec});
  rtp_video_stream_receiver_->StartReceive();

  const uint8_t av1_payload[] = {1, 2, 3, 4};
  CopyOnWriteBuffer payload(av1_payload);

  // 1. Packet 1 (first in frame) arrives.
  RtpPacketReceived rtp_packet1;
  rtp_packet1.SetPayloadType(kAv1Pt);
  rtp_packet1.SetSequenceNumber(1);
  RTPVideoHeader header1;
  header1.codec = kVideoCodecAV1;
  header1.is_first_packet_in_frame = true;
  header1.is_last_packet_in_frame = false;
  header1.frame_type = VideoFrameType::kVideoFrameKey;

  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      payload, rtp_packet1, header1);

  // 2. Packet 3 (last in frame) arrives. Packet 2 is missing.
  RtpPacketReceived rtp_packet3;
  rtp_packet3.SetPayloadType(kAv1Pt);
  rtp_packet3.SetSequenceNumber(3);
  RTPVideoHeader header3;
  header3.codec = kVideoCodecAV1;
  header3.is_first_packet_in_frame = false;
  header3.is_last_packet_in_frame = true;
  header3.frame_type = VideoFrameType::kVideoFrameKey;

  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      payload, rtp_packet3, header3);

  // 3. AV1 codec is removed via SetReceiveCodecs.
  rtp_video_stream_receiver_->SetReceiveCodecs({});

  // 4. Packet 2 arrives to complete the frame in packet_buffer_.
  // Verify frame assembly is skipped and does not crash.
  RtpPacketReceived rtp_packet2;
  rtp_packet2.SetPayloadType(kAv1Pt);
  rtp_packet2.SetSequenceNumber(2);
  RTPVideoHeader header2;
  header2.codec = kVideoCodecAV1;
  header2.is_first_packet_in_frame = false;
  header2.is_last_packet_in_frame = false;
  header2.frame_type = VideoFrameType::kVideoFrameKey;

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame).Times(0);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      payload, rtp_packet2, header2);
}

TEST_F(RtpVideoStreamReceiver2Test,
       BufferedFrameForReplacedCodecDoesNotCrashOnAssembly) {
  constexpr uint8_t kPt = 100;
  RtpVideoStreamReceiver2::ReceiveCodec av1_codec{
      .payload_type = kPt,
      .video_codec = kVideoCodecAV1,
  };

  rtp_video_stream_receiver_->SetReceiveCodecs({av1_codec});
  rtp_video_stream_receiver_->StartReceive();

  const uint8_t payload_bytes[] = {1, 2, 3, 4};
  CopyOnWriteBuffer payload(payload_bytes);

  // 1. Packet 1 (first in frame) and packet 3 (last in frame) for AV1 arrive.
  RtpPacketReceived rtp_packet1;
  rtp_packet1.SetPayloadType(kPt);
  rtp_packet1.SetSequenceNumber(1);
  RTPVideoHeader header1;
  header1.codec = kVideoCodecAV1;
  header1.is_first_packet_in_frame = true;
  header1.is_last_packet_in_frame = false;
  header1.frame_type = VideoFrameType::kVideoFrameKey;

  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      payload, rtp_packet1, header1);

  RtpPacketReceived rtp_packet3;
  rtp_packet3.SetPayloadType(kPt);
  rtp_packet3.SetSequenceNumber(3);
  RTPVideoHeader header3;
  header3.codec = kVideoCodecAV1;
  header3.is_first_packet_in_frame = false;
  header3.is_last_packet_in_frame = true;
  header3.frame_type = VideoFrameType::kVideoFrameKey;

  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      payload, rtp_packet3, header3);

  // 2. Reconfigure PT 100 to H264.
  RtpVideoStreamReceiver2::ReceiveCodec h264_codec{
      .payload_type = kPt,
      .video_codec = kVideoCodecH264,
  };
  rtp_video_stream_receiver_->SetReceiveCodecs({h264_codec});

  // 3. Packet 2 arrives to complete the AV1 frame in packet_buffer_.
  RtpPacketReceived rtp_packet2;
  rtp_packet2.SetPayloadType(kPt);
  rtp_packet2.SetSequenceNumber(2);
  RTPVideoHeader header2;
  header2.codec = kVideoCodecAV1;
  header2.is_first_packet_in_frame = false;
  header2.is_last_packet_in_frame = false;
  header2.frame_type = VideoFrameType::kVideoFrameKey;

  // H264 depacketizer concatenates payloads for the 3 packets.
  mock_on_complete_frame_callback_.AppendExpectedBitstream(payload);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(payload);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(payload);

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      payload, rtp_packet2, header2);
  mock_on_complete_frame_callback_.ClearExpectedBitstream();
}

TEST_F(RtpVideoStreamReceiver2Test,
       SetReceiveCodecsResetsH264KeyframeModeInPlace) {
  constexpr uint8_t kH264Pt = 99;

  RtpVideoStreamReceiver2::ReceiveCodec h264_sps_pps_codec{
      .payload_type = kH264Pt,
      .video_codec = kVideoCodecH264,
      .codec_params = {{std::string(kH264FmtpSpsPpsIdrInKeyframe), "1"}},
  };

  rtp_video_stream_receiver_->SetReceiveCodecs({h264_sps_pps_codec});

  // Update codecs in-place to plain H264 without sps_pps_idr_is_h264_keyframe.
  RtpVideoStreamReceiver2::ReceiveCodec plain_h264_codec{
      .payload_type = kH264Pt,
      .video_codec = kVideoCodecH264,
  };
  rtp_video_stream_receiver_->SetReceiveCodecs({plain_h264_codec});
  rtp_video_stream_receiver_->StartReceive();

  // Send SPS, PPS, and IDR packet. Because sps_pps_idr_is_h264_keyframe_ was
  // reset to false, IDR-only keyframes are allowed and frame completes.
  CopyOnWriteBuffer sps_data;
  RtpPacketReceived rtp_packet;
  RTPVideoHeader sps_video_header = GetDefaultH264VideoHeader();
  AddSps(&sps_video_header, 0, &sps_data);
  rtp_packet.SetSequenceNumber(0);
  rtp_packet.SetPayloadType(kH264Pt);
  sps_video_header.is_first_packet_in_frame = true;
  sps_video_header.frame_type = VideoFrameType::kEmptyFrame;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(sps_data);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      sps_data, rtp_packet, sps_video_header);

  CopyOnWriteBuffer pps_data;
  RTPVideoHeader pps_video_header = GetDefaultH264VideoHeader();
  AddPps(&pps_video_header, 0, 1, &pps_data);
  rtp_packet.SetSequenceNumber(1);
  rtp_packet.SetPayloadType(kH264Pt);
  pps_video_header.is_first_packet_in_frame = true;
  pps_video_header.frame_type = VideoFrameType::kEmptyFrame;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(pps_data);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      pps_data, rtp_packet, pps_video_header);

  CopyOnWriteBuffer idr_data;
  RTPVideoHeader idr_video_header = GetDefaultH264VideoHeader();
  AddIdr(&idr_video_header, 1);
  rtp_packet.SetSequenceNumber(2);
  rtp_packet.SetPayloadType(kH264Pt);
  rtp_packet.SetMarker(true);
  idr_video_header.is_first_packet_in_frame = true;
  idr_video_header.is_last_packet_in_frame = true;
  idr_video_header.frame_type = VideoFrameType::kVideoFrameKey;

  const uint8_t idr[] = {0x65, 1, 2, 3};
  idr_data.AppendData(idr);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(idr_data);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_TRUE(frame->is_keyframe()); });
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      idr_data, rtp_packet, idr_video_header);
  mock_on_complete_frame_callback_.ClearExpectedBitstream();
}

TEST_F(RtpVideoStreamReceiver2Test,
       SetReceiveCodecsTogglesH264KeyframeModeInPlace) {
  constexpr uint8_t kH264Pt = 99;

  RtpVideoStreamReceiver2::ReceiveCodec plain_h264_codec{
      .payload_type = kH264Pt,
      .video_codec = kVideoCodecH264,
  };
  rtp_video_stream_receiver_->SetReceiveCodecs({plain_h264_codec});
  rtp_video_stream_receiver_->StartReceive();

  // Toggle in-place to SPS/PPS IDR keyframe mode.
  RtpVideoStreamReceiver2::ReceiveCodec h264_sps_pps_codec{
      .payload_type = kH264Pt,
      .video_codec = kVideoCodecH264,
      .codec_params = {{std::string(kH264FmtpSpsPpsIdrInKeyframe), "1"}},
  };
  rtp_video_stream_receiver_->SetReceiveCodecs({h264_sps_pps_codec});

  // Toggle in-place back to plain H264 mode.
  rtp_video_stream_receiver_->SetReceiveCodecs({plain_h264_codec});

  // Verify IDR frame completes properly under plain H264 mode.
  CopyOnWriteBuffer sps_data;
  RtpPacketReceived rtp_packet;
  RTPVideoHeader sps_video_header = GetDefaultH264VideoHeader();
  AddSps(&sps_video_header, 0, &sps_data);
  rtp_packet.SetSequenceNumber(0);
  rtp_packet.SetPayloadType(kH264Pt);
  sps_video_header.is_first_packet_in_frame = true;
  sps_video_header.frame_type = VideoFrameType::kEmptyFrame;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(sps_data);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      sps_data, rtp_packet, sps_video_header);

  CopyOnWriteBuffer pps_data;
  RTPVideoHeader pps_video_header = GetDefaultH264VideoHeader();
  AddPps(&pps_video_header, 0, 1, &pps_data);
  rtp_packet.SetSequenceNumber(1);
  rtp_packet.SetPayloadType(kH264Pt);
  pps_video_header.is_first_packet_in_frame = true;
  pps_video_header.frame_type = VideoFrameType::kEmptyFrame;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(pps_data);
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      pps_data, rtp_packet, pps_video_header);

  CopyOnWriteBuffer idr_data;
  RTPVideoHeader idr_video_header = GetDefaultH264VideoHeader();
  AddIdr(&idr_video_header, 1);
  rtp_packet.SetSequenceNumber(2);
  rtp_packet.SetPayloadType(kH264Pt);
  rtp_packet.SetMarker(true);
  idr_video_header.is_first_packet_in_frame = true;
  idr_video_header.is_last_packet_in_frame = true;
  idr_video_header.frame_type = VideoFrameType::kVideoFrameKey;

  const uint8_t idr[] = {0x65, 1, 2, 3};
  idr_data.AppendData(idr);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(kH264StartCode);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(idr_data);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_TRUE(frame->is_keyframe()); });
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      idr_data, rtp_packet, idr_video_header);
  mock_on_complete_frame_callback_.ClearExpectedBitstream();
}

TEST_F(RtpVideoStreamReceiver2Test,
       SetReceiveCodecsPreservesDepacketizerInstanceForUnchangedCodecs) {
  constexpr uint8_t kVp8Pt = 100;

  RtpVideoStreamReceiver2::ReceiveCodec vp8_codec{
      .payload_type = kVp8Pt,
      .video_codec = kVideoCodecVP8,
  };

  rtp_video_stream_receiver_->SetReceiveCodecs({vp8_codec});
  rtp_video_stream_receiver_->StartReceive();

  // Call SetReceiveCodecs with identical codec configuration.
  rtp_video_stream_receiver_->SetReceiveCodecs({vp8_codec});

  // Verify frame delivery completes as expected.
  const uint8_t vp8_payload[] = {1, 2, 3, 4};
  CopyOnWriteBuffer payload(vp8_payload);
  RtpPacketReceived rtp_packet;
  rtp_packet.SetPayloadType(kVp8Pt);
  rtp_packet.SetSequenceNumber(1);

  RTPVideoHeader video_header;
  video_header.codec = kVideoCodecVP8;
  video_header.video_type_header.emplace<RTPVideoHeaderVP8>();
  video_header.is_first_packet_in_frame = true;
  video_header.is_last_packet_in_frame = true;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;

  mock_on_complete_frame_callback_.AppendExpectedBitstream(payload);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_TRUE(frame->is_keyframe()); });
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      payload, rtp_packet, video_header);
  mock_on_complete_frame_callback_.ClearExpectedBitstream();
}

TEST_F(RtpVideoStreamReceiver2Test,
       SetReceiveCodecsUpdatesDepacketizerWhenRawPayloadToggles) {
  constexpr uint8_t kVp8Pt = 100;

  RtpVideoStreamReceiver2::ReceiveCodec vp8_normal{
      .payload_type = kVp8Pt,
      .video_codec = kVideoCodecVP8,
      .raw_payload = false,
  };
  rtp_video_stream_receiver_->SetReceiveCodecs({vp8_normal});

  // Reconfigure in-place with raw_payload = true.
  RtpVideoStreamReceiver2::ReceiveCodec vp8_raw{
      .payload_type = kVp8Pt,
      .video_codec = kVideoCodecVP8,
      .raw_payload = true,
  };
  rtp_video_stream_receiver_->SetReceiveCodecs({vp8_raw});
  rtp_video_stream_receiver_->StartReceive();

  // Verify raw payload is delivered properly.
  const uint8_t raw_payload_bytes[] = {1, 2, 3, 4};
  CopyOnWriteBuffer payload(raw_payload_bytes);
  RtpPacketReceived rtp_packet;
  rtp_packet.SetPayloadType(kVp8Pt);
  rtp_packet.SetSequenceNumber(1);

  RTPVideoHeader video_header;
  video_header.codec = kVideoCodecVP8;
  video_header.video_type_header.emplace<RTPVideoHeaderVP8>();
  video_header.is_first_packet_in_frame = true;
  video_header.is_last_packet_in_frame = true;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;

  mock_on_complete_frame_callback_.AppendExpectedBitstream(payload);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_TRUE(frame->is_keyframe()); });
  rtp_video_stream_receiver_->OnReceivedPayloadDataForTesting(
      payload, rtp_packet, video_header);
  mock_on_complete_frame_callback_.ClearExpectedBitstream();
}

}  // namespace webrtc
