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

#include <memory>
#include <utility>

#include "api/task_queue/task_queue_base.h"
#include "api/test/mock_frame_transformer.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame_type.h"
#include "call/test/mock_rtp_packet_sink_interface.h"
#include "common_video/h264/h264_common.h"
#include "media/base/media_constants.h"
#include "modules/rtp_rtcp/source/frame_object.h"
#include "modules/rtp_rtcp/source/rtp_descriptor_authentication.h"
#include "modules/rtp_rtcp/source/rtp_format.h"
#include "modules/rtp_rtcp/source/rtp_format_vp9.h"
#include "modules/rtp_rtcp/source/rtp_generic_frame_descriptor.h"
#include "modules/rtp_rtcp/source/rtp_generic_frame_descriptor_extension.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "modules/video_coding/include/video_coding_defines.h"
#include "modules/video_coding/rtp_frame_reference_finder.h"
#include "rtc_base/byte_buffer.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/clock.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/mock_transport.h"
#include "test/rtcp_packet_parser.h"
#include "test/scoped_key_value_config.h"
#include "test/time_controller/simulated_task_queue.h"
#include "test/time_controller/simulated_time_controller.h"

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::SizeIs;
using ::testing::Values;

namespace webrtc {

namespace {

const uint8_t kH264StartCode[] = {0x00, 0x00, 0x00, 0x01};

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
    if (!frame) {
      DoOnCompleteFrameFailNullptr(nullptr);
      return;
    }
    EXPECT_EQ(buffer_.Length(), frame->size());
    if (buffer_.Length() != frame->size()) {
      DoOnCompleteFrameFailLength(frame.get());
      return;
    }
    if (frame->size() != buffer_.Length() ||
        memcmp(buffer_.Data(), frame->data(), buffer_.Length()) != 0) {
      DoOnCompleteFrameFailBitstream(frame.get());
      return;
    }
    DoOnCompleteFrame(frame.get());
  }

  void ClearExpectedBitstream() { buffer_.Clear(); }

  void AppendExpectedBitstream(const uint8_t data[], size_t size_in_bytes) {
    // TODO(Johan): Let rtc::ByteBuffer handle uint8_t* instead of char*.
    buffer_.WriteBytes(data, size_in_bytes);
  }
  rtc::ByteBufferWriter buffer_;
};

constexpr uint32_t kSsrc = 111;
constexpr int kPayloadType = 100;
constexpr int kRedPayloadType = 125;

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
        task_queue_(time_controller_.GetTaskQueueFactory()->CreateTaskQueue(
            "RtpVideoStreamReceiver2Test",
            TaskQueueFactory::Priority::NORMAL)),
        task_queue_setter_(task_queue_.get()),
        field_trials_(field_trials),
        config_(CreateConfig()) {
    rtp_receive_statistics_ =
        ReceiveStatistics::Create(Clock::GetRealTimeClock());
    rtp_video_stream_receiver_ = std::make_unique<RtpVideoStreamReceiver2>(
        TaskQueueBase::Current(), Clock::GetRealTimeClock(), &mock_transport_,
        nullptr, nullptr, &config_, rtp_receive_statistics_.get(), nullptr,
        nullptr, &nack_periodic_processor_, &mock_on_complete_frame_callback_,
        nullptr, nullptr, field_trials_, nullptr);
    rtp_video_stream_receiver_->AddReceiveCodec(kPayloadType,
                                                kVideoCodecGeneric, {},
                                                /*raw_payload=*/false);
    ON_CALL(mock_transport_, SendRtcp)
        .WillByDefault(
            Invoke(&rtcp_packet_parser_, &test::RtcpPacketParser::Parse));
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
              rtc::CopyOnWriteBuffer* data) {
    NaluInfo info;
    info.type = H264::NaluType::kSps;
    info.sps_id = sps_id;
    info.pps_id = -1;
    data->AppendData<uint8_t, 2>({H264::NaluType::kSps, sps_id});
    auto& h264 = absl::get<RTPVideoHeaderH264>(video_header->video_type_header);
    h264.nalus[h264.nalus_length++] = info;
  }

  void AddPps(RTPVideoHeader* video_header,
              uint8_t sps_id,
              uint8_t pps_id,
              rtc::CopyOnWriteBuffer* data) {
    NaluInfo info;
    info.type = H264::NaluType::kPps;
    info.sps_id = sps_id;
    info.pps_id = pps_id;
    data->AppendData<uint8_t, 2>({H264::NaluType::kPps, pps_id});
    auto& h264 = absl::get<RTPVideoHeaderH264>(video_header->video_type_header);
    h264.nalus[h264.nalus_length++] = info;
  }

  void AddIdr(RTPVideoHeader* video_header, int pps_id) {
    NaluInfo info;
    info.type = H264::NaluType::kIdr;
    info.sps_id = -1;
    info.pps_id = pps_id;
    auto& h264 = absl::get<RTPVideoHeaderH264>(video_header->video_type_header);
    h264.nalus[h264.nalus_length++] = info;
  }

  void OnRtpPacket(const RtpPacketReceived& packet) override {
    if (test_packet_sink_)
      test_packet_sink_->OnRtpPacket(packet);
  }

 protected:
  VideoReceiveStreamInterface::Config CreateConfig() {
    VideoReceiveStreamInterface::Config config(nullptr);
    config.rtp.remote_ssrc = 1111;
    config.rtp.local_ssrc = 2222;
    config.rtp.red_payload_type = kRedPayloadType;
    config.rtp.packet_sink_ = this;
    return config;
  }

  GlobalSimulatedTimeController time_controller_;
  std::unique_ptr<TaskQueueBase, TaskQueueDeleter> task_queue_;
  TokenTaskQueue::CurrentTaskQueueSetter task_queue_setter_;

  webrtc::test::ScopedKeyValueConfig field_trials_;
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
  constexpr int kVp9PayloadType = 99;
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
      extension_map.Register<ColorSpaceExtension>(1);
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
  webrtc::CodecParameterMap codec_params;
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
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      kKeyFramePayload.data(), kKeyFramePayload.size());

  // Send the key frame and expect a callback with color space information.
  EXPECT_FALSE(key_frame_packet1.GetExtension<ColorSpaceExtension>());
  EXPECT_TRUE(key_frame_packet2.GetExtension<ColorSpaceExtension>());
  rtp_video_stream_receiver_->OnRtpPacket(key_frame_packet1);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_))
      .WillOnce(Invoke([kColorSpace](EncodedFrame* frame) {
        ASSERT_TRUE(frame->EncodedImage().ColorSpace());
        EXPECT_EQ(*frame->EncodedImage().ColorSpace(), kColorSpace);
      }));
  rtp_video_stream_receiver_->OnRtpPacket(key_frame_packet2);
  // Resend the first key frame packet to simulate padding for example.
  rtp_video_stream_receiver_->OnRtpPacket(key_frame_packet1);

  mock_on_complete_frame_callback_.ClearExpectedBitstream();
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      kDeltaFramePayload.data(), kDeltaFramePayload.size());

  // Expect delta frame to have color space set even though color space not
  // included in the RTP packet.
  EXPECT_FALSE(delta_frame_packet.GetExtension<ColorSpaceExtension>());
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_))
      .WillOnce(Invoke([kColorSpace](EncodedFrame* frame) {
        ASSERT_TRUE(frame->EncodedImage().ColorSpace());
        EXPECT_EQ(*frame->EncodedImage().ColorSpace(), kColorSpace);
      }));
  rtp_video_stream_receiver_->OnRtpPacket(delta_frame_packet);
}

TEST_F(RtpVideoStreamReceiver2Test, GenericKeyFrame) {
  RtpPacketReceived rtp_packet;
  rtc::CopyOnWriteBuffer data({'1', '2', '3', '4'});
  rtp_packet.SetPayloadType(kPayloadType);
  rtp_packet.SetSequenceNumber(1);
  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameKey);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data.data(),
                                                           data.size());
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  rtp_video_stream_receiver_->OnReceivedPayloadData(data, rtp_packet,
                                                    video_header, 0);
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
  constexpr int kId0 = 1;

  RtpHeaderExtensionMap extension_map;
  extension_map.Register<AbsoluteCaptureTimeExtension>(kId0);
  RtpPacketReceived rtp_packet(&extension_map);
  rtp_packet.SetPayloadType(kPayloadType);
  rtc::CopyOnWriteBuffer data({'1', '2', '3', '4'});
  rtp_packet.SetSequenceNumber(1);
  rtp_packet.SetTimestamp(1);
  rtp_packet.SetSsrc(kSsrc);
  rtp_packet.SetExtension<AbsoluteCaptureTimeExtension>(
      AbsoluteCaptureTime{kAbsoluteCaptureTimestamp,
                          /*estimated_capture_clock_offset=*/absl::nullopt});

  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameKey);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data.data(),
                                                           data.size());
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_))
      .WillOnce(Invoke([kAbsoluteCaptureTimestamp](EncodedFrame* frame) {
        EXPECT_THAT(GetAbsoluteCaptureTimestamps(frame),
                    ElementsAre(kAbsoluteCaptureTimestamp));
      }));
  rtp_video_stream_receiver_->OnReceivedPayloadData(data, rtp_packet,
                                                    video_header, 0);
}

TEST_F(RtpVideoStreamReceiver2Test,
       MissingAbsoluteCaptureTimeIsFilledWithExtrapolatedValue) {
  constexpr uint64_t kAbsoluteCaptureTimestamp = 12;
  constexpr int kId0 = 1;

  RtpHeaderExtensionMap extension_map;
  extension_map.Register<AbsoluteCaptureTimeExtension>(kId0);
  RtpPacketReceived rtp_packet(&extension_map);
  rtp_packet.SetPayloadType(kPayloadType);

  rtc::CopyOnWriteBuffer data({'1', '2', '3', '4'});
  uint16_t sequence_number = 1;
  uint32_t rtp_timestamp = 1;
  rtp_packet.SetSequenceNumber(sequence_number);
  rtp_packet.SetTimestamp(rtp_timestamp);
  rtp_packet.SetSsrc(kSsrc);
  rtp_packet.SetExtension<AbsoluteCaptureTimeExtension>(
      AbsoluteCaptureTime{kAbsoluteCaptureTimestamp,
                          /*estimated_capture_clock_offset=*/absl::nullopt});

  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameKey);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data.data(),
                                                           data.size());
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  rtp_video_stream_receiver_->OnReceivedPayloadData(data, rtp_packet,
                                                    video_header, 0);

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
      .WillOnce(Invoke([](EncodedFrame* frame) {
        EXPECT_THAT(GetAbsoluteCaptureTimestamps(frame), SizeIs(1));
      }));
  rtp_video_stream_receiver_->OnReceivedPayloadData(data, rtp_packet,
                                                    video_header, 0);
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
  const uint8_t kRedPayloadType = 125;
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
  EXPECT_TRUE(packet.Parse(rtc::CopyOnWriteBuffer(data)));
  rtp_video_stream_receiver_->StartReceive();
  rtp_video_stream_receiver_->OnRtpPacket(packet);
  // Expect asan doesn't find anything.
}

TEST_F(RtpVideoStreamReceiver2Test, GenericKeyFrameBitstreamError) {
  RtpPacketReceived rtp_packet;
  rtp_packet.SetPayloadType(kPayloadType);
  rtc::CopyOnWriteBuffer data({'1', '2', '3', '4'});
  rtp_packet.SetSequenceNumber(1);
  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameKey);
  constexpr uint8_t expected_bitsteam[] = {1, 2, 3, 0xff};
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      expected_bitsteam, sizeof(expected_bitsteam));
  EXPECT_CALL(mock_on_complete_frame_callback_,
              DoOnCompleteFrameFailBitstream(_));
  rtp_video_stream_receiver_->OnReceivedPayloadData(data, rtp_packet,
                                                    video_header, 0);
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
  webrtc::CodecParameterMap codec_params;
  rtp_video_stream_receiver_->AddReceiveCodec(kH264PayloadType, kVideoCodecH264,
                                              codec_params,
                                              /*raw_payload=*/false);
  rtp_video_stream_receiver_->StartReceive();

  rtc::CopyOnWriteBuffer sps_data;
  RtpPacketReceived rtp_packet;
  RTPVideoHeader sps_video_header = GetDefaultH264VideoHeader();
  AddSps(&sps_video_header, 0, &sps_data);
  rtp_packet.SetSequenceNumber(0);
  rtp_packet.SetPayloadType(kH264PayloadType);
  sps_video_header.is_first_packet_in_frame = true;
  sps_video_header.frame_type = VideoFrameType::kEmptyFrame;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      kH264StartCode, sizeof(kH264StartCode));
  mock_on_complete_frame_callback_.AppendExpectedBitstream(sps_data.data(),
                                                           sps_data.size());
  rtp_video_stream_receiver_->OnReceivedPayloadData(sps_data, rtp_packet,
                                                    sps_video_header, 0);

  rtc::CopyOnWriteBuffer pps_data;
  RTPVideoHeader pps_video_header = GetDefaultH264VideoHeader();
  AddPps(&pps_video_header, 0, 1, &pps_data);
  rtp_packet.SetSequenceNumber(1);
  rtp_packet.SetPayloadType(kH264PayloadType);
  pps_video_header.is_first_packet_in_frame = true;
  pps_video_header.frame_type = VideoFrameType::kEmptyFrame;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      kH264StartCode, sizeof(kH264StartCode));
  mock_on_complete_frame_callback_.AppendExpectedBitstream(pps_data.data(),
                                                           pps_data.size());
  rtp_video_stream_receiver_->OnReceivedPayloadData(pps_data, rtp_packet,
                                                    pps_video_header, 0);

  rtc::CopyOnWriteBuffer idr_data;
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
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      kH264StartCode, sizeof(kH264StartCode));
  mock_on_complete_frame_callback_.AppendExpectedBitstream(idr_data.data(),
                                                           idr_data.size());
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  rtp_video_stream_receiver_->OnReceivedPayloadData(idr_data, rtp_packet,
                                                    idr_video_header, 0);
}

TEST_P(RtpVideoStreamReceiver2TestH264, OutOfBandFmtpSpsPps) {
  constexpr int kPayloadType = 99;
  webrtc::CodecParameterMap codec_params;
  // Example parameter sets from https://tools.ietf.org/html/rfc3984#section-8.2
  // .
  codec_params.insert(
      {cricket::kH264FmtpSpropParameterSets, "Z0IACpZTBYmI,aMljiA=="});
  rtp_video_stream_receiver_->AddReceiveCodec(kPayloadType, kVideoCodecH264,
                                              codec_params,
                                              /*raw_payload=*/false);
  rtp_video_stream_receiver_->StartReceive();
  const uint8_t binary_sps[] = {0x67, 0x42, 0x00, 0x0a, 0x96,
                                0x53, 0x05, 0x89, 0x88};
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      kH264StartCode, sizeof(kH264StartCode));
  mock_on_complete_frame_callback_.AppendExpectedBitstream(binary_sps,
                                                           sizeof(binary_sps));
  const uint8_t binary_pps[] = {0x68, 0xc9, 0x63, 0x88};
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      kH264StartCode, sizeof(kH264StartCode));
  mock_on_complete_frame_callback_.AppendExpectedBitstream(binary_pps,
                                                           sizeof(binary_pps));

  RtpPacketReceived rtp_packet;
  RTPVideoHeader video_header = GetDefaultH264VideoHeader();
  AddIdr(&video_header, 0);
  rtp_packet.SetPayloadType(kPayloadType);
  rtp_packet.SetSequenceNumber(2);
  rtp_packet.SetMarker(true);
  video_header.is_first_packet_in_frame = true;
  video_header.is_last_packet_in_frame = true;
  video_header.codec = kVideoCodecH264;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;
  rtc::CopyOnWriteBuffer data({'1', '2', '3'});
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      kH264StartCode, sizeof(kH264StartCode));
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data.data(),
                                                           data.size());
  // IDR frames without SPS/PPS are not returned by
  // |H26xPacketBuffer.InsertPacket| until SPS and PPS are received when
  // WebRTC-SpsPpsIdrIsH264Keyframe is enabled.
  if (!field_trials_.IsEnabled("WebRTC-SpsPpsIdrIsH264Keyframe") ||
      !field_trials_.IsEnabled("WebRTC-Video-H26xPacketBuffer")) {
    EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  }
  rtp_video_stream_receiver_->OnReceivedPayloadData(data, rtp_packet,
                                                    video_header, 0);
}

TEST_P(RtpVideoStreamReceiver2TestH264, ForceSpsPpsIdrIsKeyframe) {
  constexpr int kPayloadType = 99;
  webrtc::CodecParameterMap codec_params;
  // Forcing can be done either with field trial or codec_params.
  if (!field_trials_.IsEnabled("WebRTC-SpsPpsIdrIsH264Keyframe")) {
    codec_params.insert({cricket::kH264FmtpSpsPpsIdrInKeyframe, ""});
  }
  rtp_video_stream_receiver_->AddReceiveCodec(kPayloadType, kVideoCodecH264,
                                              codec_params,
                                              /*raw_payload=*/false);
  rtp_video_stream_receiver_->StartReceive();
  rtc::CopyOnWriteBuffer sps_data;
  RtpPacketReceived rtp_packet;
  RTPVideoHeader sps_video_header = GetDefaultH264VideoHeader();
  AddSps(&sps_video_header, 0, &sps_data);
  rtp_packet.SetSequenceNumber(0);
  rtp_packet.SetPayloadType(kPayloadType);
  sps_video_header.is_first_packet_in_frame = true;
  sps_video_header.frame_type = VideoFrameType::kEmptyFrame;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      kH264StartCode, sizeof(kH264StartCode));
  mock_on_complete_frame_callback_.AppendExpectedBitstream(sps_data.data(),
                                                           sps_data.size());
  rtp_video_stream_receiver_->OnReceivedPayloadData(sps_data, rtp_packet,
                                                    sps_video_header, 0);

  rtc::CopyOnWriteBuffer pps_data;
  RTPVideoHeader pps_video_header = GetDefaultH264VideoHeader();
  AddPps(&pps_video_header, 0, 1, &pps_data);
  rtp_packet.SetSequenceNumber(1);
  pps_video_header.is_first_packet_in_frame = true;
  pps_video_header.frame_type = VideoFrameType::kEmptyFrame;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      kH264StartCode, sizeof(kH264StartCode));
  mock_on_complete_frame_callback_.AppendExpectedBitstream(pps_data.data(),
                                                           pps_data.size());
  rtp_video_stream_receiver_->OnReceivedPayloadData(pps_data, rtp_packet,
                                                    pps_video_header, 0);

  rtc::CopyOnWriteBuffer idr_data;
  RTPVideoHeader idr_video_header = GetDefaultH264VideoHeader();
  AddIdr(&idr_video_header, 1);
  rtp_packet.SetSequenceNumber(2);
  rtp_packet.SetMarker(true);
  idr_video_header.is_first_packet_in_frame = true;
  idr_video_header.is_last_packet_in_frame = true;
  idr_video_header.frame_type = VideoFrameType::kVideoFrameKey;
  const uint8_t idr[] = {0x65, 1, 2, 3};
  idr_data.AppendData(idr);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      kH264StartCode, sizeof(kH264StartCode));
  mock_on_complete_frame_callback_.AppendExpectedBitstream(idr_data.data(),
                                                           idr_data.size());
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce(
          [&](EncodedFrame* frame) { EXPECT_TRUE(frame->is_keyframe()); });
  rtp_video_stream_receiver_->OnReceivedPayloadData(idr_data, rtp_packet,
                                                    idr_video_header, 0);
  mock_on_complete_frame_callback_.ClearExpectedBitstream();
  mock_on_complete_frame_callback_.AppendExpectedBitstream(
      kH264StartCode, sizeof(kH264StartCode));
  mock_on_complete_frame_callback_.AppendExpectedBitstream(idr_data.data(),
                                                           idr_data.size());
  rtp_packet.SetSequenceNumber(3);
  // IDR frames without SPS/PPS are not returned by
  // |H26xPacketBuffer.InsertPacket| until SPS and PPS are received, while
  // |PacketBuffer| returns it as a delta frame.
  if (field_trials_.IsEnabled("WebRTC-Video-H26xPacketBuffer")) {
    EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame).Times(0);
  } else {
    EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
        .WillOnce(
            [&](EncodedFrame* frame) { EXPECT_FALSE(frame->is_keyframe()); });
  }
  rtp_video_stream_receiver_->OnReceivedPayloadData(idr_data, rtp_packet,
                                                    idr_video_header, 0);
}

TEST_F(RtpVideoStreamReceiver2Test, PaddingInMediaStream) {
  RtpPacketReceived rtp_packet;
  RTPVideoHeader video_header = GetDefaultH264VideoHeader();
  rtc::CopyOnWriteBuffer data({'1', '2', '3'});
  rtp_packet.SetPayloadType(kPayloadType);
  rtp_packet.SetSequenceNumber(2);
  video_header.is_first_packet_in_frame = true;
  video_header.is_last_packet_in_frame = true;
  video_header.codec = kVideoCodecGeneric;
  video_header.frame_type = VideoFrameType::kVideoFrameKey;
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data.data(),
                                                           data.size());

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  rtp_video_stream_receiver_->OnReceivedPayloadData(data, rtp_packet,
                                                    video_header, 0);

  rtp_packet.SetSequenceNumber(3);
  rtp_video_stream_receiver_->OnReceivedPayloadData({}, rtp_packet,
                                                    video_header, 0);

  rtp_packet.SetSequenceNumber(4);
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  video_header.frame_type = VideoFrameType::kVideoFrameDelta;
  rtp_video_stream_receiver_->OnReceivedPayloadData(data, rtp_packet,
                                                    video_header, 0);

  rtp_packet.SetSequenceNumber(6);
  rtp_video_stream_receiver_->OnReceivedPayloadData(data, rtp_packet,
                                                    video_header, 0);

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  rtp_packet.SetSequenceNumber(5);
  rtp_video_stream_receiver_->OnReceivedPayloadData({}, rtp_packet,
                                                    video_header, 0);
}

TEST_F(RtpVideoStreamReceiver2Test, RequestKeyframeIfFirstFrameIsDelta) {
  RtpPacketReceived rtp_packet;
  rtp_packet.SetPayloadType(kPayloadType);
  rtc::CopyOnWriteBuffer data({'1', '2', '3', '4'});
  rtp_packet.SetSequenceNumber(1);
  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameDelta);

  rtp_video_stream_receiver_->OnReceivedPayloadData(data, rtp_packet,
                                                    video_header, 0);
  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(1));
}

TEST_F(RtpVideoStreamReceiver2Test, RequestKeyframeWhenPacketBufferGetsFull) {
  constexpr int kPacketBufferMaxSize = 2048;

  RtpPacketReceived rtp_packet;
  rtp_packet.SetPayloadType(kPayloadType);
  rtc::CopyOnWriteBuffer data({'1', '2', '3', '4'});
  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameDelta);
  // Incomplete frames so that the packet buffer is filling up.
  video_header.is_last_packet_in_frame = false;
  uint16_t start_sequence_number = 1234;
  rtp_packet.SetSequenceNumber(start_sequence_number);
  while (rtp_packet.SequenceNumber() - start_sequence_number <
         kPacketBufferMaxSize) {
    rtp_video_stream_receiver_->OnReceivedPayloadData(data, rtp_packet,
                                                      video_header, 0);
    rtp_packet.SetSequenceNumber(rtp_packet.SequenceNumber() + 2);
  }

  rtp_video_stream_receiver_->OnReceivedPayloadData(data, rtp_packet,
                                                    video_header, 0);
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
  extension_map.Register<RtpGenericFrameDescriptorExtension00>(5);
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

  uint8_t* payload = rtp_packet.SetPayloadSize(data.size());
  memcpy(payload, data.data(), data.size());
  // The first byte is the header, so we ignore the first byte of `data`.
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data.data() + 1,
                                                           data.size() - 1);

  rtp_packet.SetMarker(true);
  rtp_packet.SetPayloadType(kPayloadType);
  rtp_packet.SetSequenceNumber(1);

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce(Invoke([kSpatialIndex](EncodedFrame* frame) {
        EXPECT_EQ(frame->num_references, 2U);
        EXPECT_EQ(frame->references[0], frame->Id() - 90);
        EXPECT_EQ(frame->references[1], frame->Id() - 80);
        EXPECT_EQ(frame->SpatialIndex(), kSpatialIndex);
        EXPECT_THAT(frame->PacketInfos(), SizeIs(1));
      }));

  rtp_video_stream_receiver_->OnRtpPacket(rtp_packet);
}

TEST_F(RtpVideoStreamReceiver2Test, ParseGenericDescriptorTwoPackets) {
  const std::vector<uint8_t> data = {0, 1, 2, 3, 4};
  const int kSpatialIndex = 1;

  rtp_video_stream_receiver_->StartReceive();

  RtpHeaderExtensionMap extension_map;
  extension_map.Register<RtpGenericFrameDescriptorExtension00>(5);
  RtpPacketReceived first_packet(&extension_map);

  RtpGenericFrameDescriptor first_packet_descriptor;
  first_packet_descriptor.SetFirstPacketInSubFrame(true);
  first_packet_descriptor.SetLastPacketInSubFrame(false);
  first_packet_descriptor.SetFrameId(100);
  first_packet_descriptor.SetSpatialLayersBitmask(1 << kSpatialIndex);
  first_packet_descriptor.SetResolution(480, 360);
  ASSERT_TRUE(first_packet.SetExtension<RtpGenericFrameDescriptorExtension00>(
      first_packet_descriptor));

  uint8_t* first_packet_payload = first_packet.SetPayloadSize(data.size());
  memcpy(first_packet_payload, data.data(), data.size());
  // The first byte is the header, so we ignore the first byte of `data`.
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data.data() + 1,
                                                           data.size() - 1);

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

  uint8_t* second_packet_payload = second_packet.SetPayloadSize(data.size());
  memcpy(second_packet_payload, data.data(), data.size());
  // The first byte is the header, so we ignore the first byte of `data`.
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data.data() + 1,
                                                           data.size() - 1);

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame)
      .WillOnce(Invoke([kSpatialIndex](EncodedFrame* frame) {
        EXPECT_EQ(frame->num_references, 0U);
        EXPECT_EQ(frame->SpatialIndex(), kSpatialIndex);
        EXPECT_EQ(frame->EncodedImage()._encodedWidth, 480u);
        EXPECT_EQ(frame->EncodedImage()._encodedHeight, 360u);
        EXPECT_THAT(frame->PacketInfos(), SizeIs(2));
      }));

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
  extension_map.Register<RtpGenericFrameDescriptorExtension00>(5);
  RtpPacketReceived rtp_packet(&extension_map);

  RtpGenericFrameDescriptor generic_descriptor;
  generic_descriptor.SetFirstPacketInSubFrame(true);
  generic_descriptor.SetLastPacketInSubFrame(true);
  ASSERT_TRUE(rtp_packet.SetExtension<RtpGenericFrameDescriptorExtension00>(
      generic_descriptor));

  uint8_t* payload = rtp_packet.SetPayloadSize(data.size());
  memcpy(payload, data.data(), data.size());
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data.data(),
                                                           data.size());

  rtp_packet.SetMarker(true);
  rtp_packet.SetPayloadType(kRawPayloadType);
  rtp_packet.SetSequenceNumber(1);

  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame);
  rtp_video_stream_receiver_->OnRtpPacket(rtp_packet);
}

TEST_F(RtpVideoStreamReceiver2Test, UnwrapsFrameId) {
  const std::vector<uint8_t> data = {0, 1, 2, 3, 4};
  const int kPayloadType = 123;

  rtp_video_stream_receiver_->AddReceiveCodec(kPayloadType, kVideoCodecGeneric,
                                              {},
                                              /*raw_payload=*/true);
  rtp_video_stream_receiver_->StartReceive();
  RtpHeaderExtensionMap extension_map;
  extension_map.Register<RtpGenericFrameDescriptorExtension00>(5);

  uint16_t rtp_sequence_number = 1;
  auto inject_packet = [&](uint16_t wrapped_frame_id) {
    RtpPacketReceived rtp_packet(&extension_map);

    RtpGenericFrameDescriptor generic_descriptor;
    generic_descriptor.SetFirstPacketInSubFrame(true);
    generic_descriptor.SetLastPacketInSubFrame(true);
    generic_descriptor.SetFrameId(wrapped_frame_id);
    ASSERT_TRUE(rtp_packet.SetExtension<RtpGenericFrameDescriptorExtension00>(
        generic_descriptor));

    uint8_t* payload = rtp_packet.SetPayloadSize(data.size());
    ASSERT_TRUE(payload);
    memcpy(payload, data.data(), data.size());
    mock_on_complete_frame_callback_.ClearExpectedBitstream();
    mock_on_complete_frame_callback_.AppendExpectedBitstream(data.data(),
                                                             data.size());
    rtp_packet.SetMarker(true);
    rtp_packet.SetPayloadType(kPayloadType);
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
    extension_map_.Register<RtpDependencyDescriptorExtension>(7);
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
    const std::vector<uint8_t> data = {0, 1, 2, 3, 4};
    RtpPacketReceived rtp_packet(&extension_map_);
    ASSERT_TRUE(rtp_packet.SetExtension<RtpDependencyDescriptorExtension>(
        stream_structure, dependency_descriptor));
    uint8_t* payload = rtp_packet.SetPayloadSize(data.size());
    ASSERT_TRUE(payload);
    memcpy(payload, data.data(), data.size());
    mock_on_complete_frame_callback_.ClearExpectedBitstream();
    mock_on_complete_frame_callback_.AppendExpectedBitstream(data.data(),
                                                             data.size());
    rtp_packet.SetMarker(true);
    rtp_packet.SetPayloadType(payload_type_);
    rtp_packet.SetSequenceNumber(++rtp_sequence_number_);
    rtp_video_stream_receiver_->OnRtpPacket(rtp_packet);
  }

 private:
  const int payload_type_ = 123;
  RtpHeaderExtensionMap extension_map_;
  uint16_t rtp_sequence_number_ = 321;
};

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
  deltaframe1_descriptor.frame_dependencies = stream_structure.templates[1];
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
  rtc::scoped_refptr<MockFrameTransformer> mock_frame_transformer =
      rtc::make_ref_counted<testing::NiceMock<MockFrameTransformer>>();
  EXPECT_CALL(*mock_frame_transformer,
              RegisterTransformedFrameSinkCallback(_, config_.rtp.remote_ssrc));
  auto receiver = std::make_unique<RtpVideoStreamReceiver2>(
      TaskQueueBase::Current(), Clock::GetRealTimeClock(), &mock_transport_,
      nullptr, nullptr, &config_, rtp_receive_statistics_.get(), nullptr,
      nullptr, &nack_periodic_processor_, &mock_on_complete_frame_callback_,
      nullptr, mock_frame_transformer, field_trials_, nullptr);
  receiver->AddReceiveCodec(kPayloadType, kVideoCodecGeneric, {},
                            /*raw_payload=*/false);

  RtpPacketReceived rtp_packet;
  rtp_packet.SetPayloadType(kPayloadType);
  rtc::CopyOnWriteBuffer data({'1', '2', '3', '4'});
  rtp_packet.SetSequenceNumber(1);
  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameKey);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(data.data(),
                                                           data.size());
  EXPECT_CALL(*mock_frame_transformer, Transform(_));
  receiver->OnReceivedPayloadData(data, rtp_packet, video_header, 0);

  EXPECT_CALL(*mock_frame_transformer,
              UnregisterTransformedFrameSinkCallback(config_.rtp.remote_ssrc));
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
  rtc::CopyOnWriteBuffer payload_data({'1', '2', '3', '4'});
  RtpHeaderExtensionMap extension_map;
  extension_map.Register<PlayoutDelayLimits>(1);
  RtpPacketToSend packet_to_send(&extension_map);
  packet_to_send.SetPayloadType(kPayloadType);
  packet_to_send.SetSequenceNumber(1);

  // Set playout delay on outgoing packet.
  EXPECT_TRUE(packet_to_send.SetExtension<PlayoutDelayLimits>(
      TransmittedPlayoutDelay()));
  uint8_t* payload = packet_to_send.AllocatePayload(payload_data.size());
  memcpy(payload, payload_data.data(), payload_data.size());

  RtpPacketReceived received_packet(&extension_map);
  received_packet.Parse(packet_to_send.data(), packet_to_send.size());

  RTPVideoHeader video_header =
      GetGenericVideoHeader(VideoFrameType::kVideoFrameKey);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(payload_data.data(),
                                                           payload_data.size());
  // Expect the playout delay of encoded frame to be the same as the transmitted
  // playout delay unless it was overridden by a field trial.
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_))
      .WillOnce(Invoke([expected_playout_delay =
                            GetParam().expected_delay](EncodedFrame* frame) {
        EXPECT_EQ(frame->EncodedImage().PlayoutDelay(), expected_playout_delay);
      }));
  rtp_video_stream_receiver_->OnReceivedPayloadData(
      received_packet.PayloadBuffer(), received_packet, video_header, 0);
}

#ifdef RTC_ENABLE_H265
RTPVideoHeader GetDefaultH265VideoHeader() {
  RTPVideoHeader video_header;
  video_header.codec = kVideoCodecH265;
  return video_header;
}

class RtpVideoStreamReceiver2TestH265 : public RtpVideoStreamReceiver2Test {
 protected:
  RtpVideoStreamReceiver2TestH265()
      : RtpVideoStreamReceiver2Test("WebRTC-Video-H26xPacketBuffer/Enabled/") {}
};

TEST_F(RtpVideoStreamReceiver2TestH265, H265Bitstream) {
  constexpr int kH265PayloadType = 98;
  webrtc::CodecParameterMap codec_params;
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
  mock_on_complete_frame_callback_.AppendExpectedBitstream(vps, sizeof(vps));
  rtp_video_stream_receiver_->OnReceivedPayloadData(
      rtc::CopyOnWriteBuffer(vps, sizeof(vps)), rtp_packet, video_header, 0);

  rtp_packet.SetSequenceNumber(1);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(sps, sizeof(sps));
  rtp_video_stream_receiver_->OnReceivedPayloadData(
      rtc::CopyOnWriteBuffer(sps, sizeof(sps)), rtp_packet, video_header, 0);

  rtp_packet.SetSequenceNumber(2);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(pps, sizeof(pps));
  rtp_video_stream_receiver_->OnReceivedPayloadData(
      rtc::CopyOnWriteBuffer(pps, sizeof(pps)), rtp_packet, video_header, 0);

  rtp_packet.SetSequenceNumber(3);
  rtp_packet.SetMarker(true);
  mock_on_complete_frame_callback_.AppendExpectedBitstream(idr, sizeof(idr));
  EXPECT_CALL(mock_on_complete_frame_callback_, DoOnCompleteFrame(_));
  rtp_video_stream_receiver_->OnReceivedPayloadData(
      rtc::CopyOnWriteBuffer(idr, sizeof(idr)), rtp_packet, video_header, 0);
}
#endif  // RTC_ENABLE_H265

}  // namespace webrtc
