/*
 *  Copyright (c) 2023 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/rtp_sender_video_frame_transformer_delegate.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "api/frame_transformer_interface.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/test/mock_frame_transformer.h"
#include "api/test/mock_transformable_video_frame.h"
#include "api/transport/rtp/dependency_descriptor.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/encoded_image.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame_metadata.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_layers_allocation.h"
#include "modules/rtp_rtcp/source/rtp_video_header.h"
#include "modules/video_coding/codecs/vp8/include/vp8_globals.h"
#include "rtc_base/event.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/time_controller/simulated_time_controller.h"

namespace webrtc {
namespace {

using ::testing::_;
using ::testing::ElementsAreArray;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::WithoutArgs;

class MockRTPVideoFrameSenderInterface : public RTPVideoFrameSenderInterface {
 public:
  MOCK_METHOD(bool,
              SendVideo,
              (int payload_type,
               VideoCodecType codec_type,
               uint32_t rtp_timestamp,
               Timestamp capture_time,
               std::span<const uint8_t> payload,
               size_t encoder_output_size,
               RTPVideoHeader video_header,
               TimeDelta expected_retransmission_time,
               std::vector<uint32_t> csrcs),
              (override));

  MOCK_METHOD(bool,
              SendVideoFrame,
              (int payload_type,
               VideoCodecType codec_type,
               RtpTimestampInfo rtp_timestamp_info,
               Timestamp capture_time,
               std::span<const uint8_t> payload,
               size_t encoder_output_size,
               RTPVideoHeader video_header,
               TimeDelta expected_retransmission_time,
               std::vector<uint32_t> csrcs),
              (override));

  MOCK_METHOD(void,
              SetVideoStructureAfterTransformation,
              (const FrameDependencyStructure* video_structure),
              (override));
  MOCK_METHOD(void,
              SetVideoLayersAllocationAfterTransformation,
              (VideoLayersAllocation allocation),
              (override));
};

class RtpSenderVideoFrameTransformerDelegateTest : public ::testing::Test {
 protected:
  RtpSenderVideoFrameTransformerDelegateTest()
      : frame_transformer_(make_ref_counted<MockFrameTransformer>()),
        time_controller_(Timestamp::Seconds(0)) {}

  ~RtpSenderVideoFrameTransformerDelegateTest() override = default;

  std::unique_ptr<TransformableFrameInterface> GetTransformableFrame(
      scoped_refptr<RTPSenderVideoFrameTransformerDelegate> delegate,
      bool key_frame = false) {
    EncodedImage encoded_image;
    encoded_image.SetEncodedData(EncodedImageBuffer::Create(1));
    encoded_image.set_frame_type(key_frame ? VideoFrameType::kVideoFrameKey
                                           : VideoFrameType::kVideoFrameDelta);
    std::unique_ptr<TransformableFrameInterface> frame = nullptr;
    EXPECT_CALL(*frame_transformer_, Transform)
        .WillOnce([&](std::unique_ptr<TransformableFrameInterface>
                          frame_to_transform) {
          frame = std::move(frame_to_transform);
        });
    RTPVideoHeader rtp_header;

    VideoFrameMetadata metadata;
    metadata.SetCodec(VideoCodecType::kVideoCodecVP8);
    metadata.SetRTPVideoHeaderCodecSpecifics(RTPVideoHeaderVP8());

    delegate->TransformFrame(
        /*payload_type=*/1, VideoCodecType::kVideoCodecVP8, /*rtp_timestamp=*/2,
        encoded_image, RTPVideoHeader::FromMetadata(metadata),
        /*expected_retransmission_time=*/TimeDelta::Millis(10));
    return frame;
  }

  MockRTPVideoFrameSenderInterface test_sender_;
  scoped_refptr<MockFrameTransformer> frame_transformer_;
  GlobalSimulatedTimeController time_controller_;
};

TEST_F(RtpSenderVideoFrameTransformerDelegateTest,
       RegisterTransformedFrameCallbackSinkOnInit) {
  auto delegate = make_ref_counted<RTPSenderVideoFrameTransformerDelegate>(
      &test_sender_, frame_transformer_,
      /*ssrc=*/1111, /*rid=*/"rid",
      time_controller_.CreateTaskQueueFactory().get());
  EXPECT_CALL(*frame_transformer_,
              RegisterTransformedFrameSinkCallback(_, 1111));
  delegate->Init();
}

TEST_F(RtpSenderVideoFrameTransformerDelegateTest,
       UnregisterTransformedFrameSinkCallbackOnReset) {
  auto delegate = make_ref_counted<RTPSenderVideoFrameTransformerDelegate>(
      &test_sender_, frame_transformer_,
      /*ssrc=*/1111, /*rid=*/"rid",
      time_controller_.CreateTaskQueueFactory().get());
  EXPECT_CALL(*frame_transformer_,
              UnregisterTransformedFrameSinkCallback(1111));
  delegate->Reset();
}

TEST_F(RtpSenderVideoFrameTransformerDelegateTest,
       TransformFrameCallsTransform) {
  auto delegate = make_ref_counted<RTPSenderVideoFrameTransformerDelegate>(
      &test_sender_, frame_transformer_,
      /*ssrc=*/1111, /*rid=*/"myrid",
      time_controller_.CreateTaskQueueFactory().get());
  VideoFrameMetadata metadata;
  std::optional<std::string> rid;
  EXPECT_CALL(*frame_transformer_, Transform)
      .WillOnce([&](std::unique_ptr<TransformableFrameInterface> frame) {
        TransformableVideoFrameInterface* video_frame =
            static_cast<TransformableVideoFrameInterface*>(frame.get());
        metadata = video_frame->Metadata();
        rid = video_frame->Rid();
      });

  std::vector<uint32_t> csrcs = {1, 2, 3};
  EncodedImage encoded_image;
  delegate->TransformFrame(
      /*payload_type=*/1, VideoCodecType::kVideoCodecVP8, /*rtp_timestamp=*/2,
      encoded_image, RTPVideoHeader(),
      /*expected_retransmission_time=*/TimeDelta::Millis(10), csrcs);

  EXPECT_EQ(metadata.GetSsrc(), 1111U);
  EXPECT_THAT(metadata.GetCsrcs(), ElementsAreArray(csrcs));
  EXPECT_EQ(rid, "myrid");
}

TEST_F(RtpSenderVideoFrameTransformerDelegateTest,
       OnTransformedFrameCallsSenderSendVideo) {
  auto delegate = make_ref_counted<RTPSenderVideoFrameTransformerDelegate>(
      &test_sender_, frame_transformer_,
      /*ssrc=*/1111, /*rid=*/"rid",
      time_controller_.CreateTaskQueueFactory().get());

  scoped_refptr<TransformedFrameCallback> callback;
  EXPECT_CALL(*frame_transformer_, RegisterTransformedFrameSinkCallback)
      .WillOnce(SaveArg<0>(&callback));
  delegate->Init();
  ASSERT_TRUE(callback);

  std::unique_ptr<TransformableFrameInterface> frame =
      GetTransformableFrame(delegate);
  ASSERT_TRUE(frame);
  EXPECT_STRCASEEQ("video/VP8", frame->GetMimeType().c_str());

  Event event;
  EXPECT_CALL(test_sender_, SendVideoFrame).WillOnce(WithoutArgs([&] {
    event.Set();
    return true;
  }));

  callback->OnTransformedFrame(std::move(frame));

  event.Wait(TimeDelta::Seconds(1));
}

TEST_F(RtpSenderVideoFrameTransformerDelegateTest, CloneSenderVideoFrame) {
  auto delegate = make_ref_counted<RTPSenderVideoFrameTransformerDelegate>(
      &test_sender_, frame_transformer_,
      /*ssrc=*/1111, /*rid=*/"rid",
      time_controller_.CreateTaskQueueFactory().get());

  std::unique_ptr<TransformableFrameInterface> frame =
      GetTransformableFrame(delegate);
  ASSERT_TRUE(frame);

  auto& video_frame = static_cast<TransformableVideoFrameInterface&>(*frame);
  std::unique_ptr<TransformableVideoFrameInterface> clone =
      CloneSenderVideoFrame(&video_frame);

  EXPECT_EQ(clone->IsKeyFrame(), video_frame.IsKeyFrame());
  EXPECT_EQ(clone->GetPayloadType(), video_frame.GetPayloadType());
  EXPECT_EQ(clone->GetMimeType(), video_frame.GetMimeType());
  EXPECT_EQ(clone->GetSsrc(), video_frame.GetSsrc());
  EXPECT_TRUE(std::holds_alternative<RtpTimestampWithOffset>(
      clone->GetRtpTimestampInfo()));
  EXPECT_TRUE(std::holds_alternative<RtpTimestampWithOffset>(
      video_frame.GetRtpTimestampInfo()));
  EXPECT_EQ(
      std::get<RtpTimestampWithOffset>(clone->GetRtpTimestampInfo()),
      std::get<RtpTimestampWithOffset>(video_frame.GetRtpTimestampInfo()));
  EXPECT_EQ(clone->Metadata(), video_frame.Metadata());
  EXPECT_EQ(clone->Rid(), video_frame.Rid());
}

TEST_F(RtpSenderVideoFrameTransformerDelegateTest, CloneKeyFrame) {
  auto delegate = make_ref_counted<RTPSenderVideoFrameTransformerDelegate>(
      &test_sender_, frame_transformer_,
      /*ssrc=*/1111, /*rid=*/"rid",
      time_controller_.CreateTaskQueueFactory().get());

  std::unique_ptr<TransformableFrameInterface> frame =
      GetTransformableFrame(delegate, /*key_frame=*/true);
  ASSERT_TRUE(frame);

  auto& video_frame = static_cast<TransformableVideoFrameInterface&>(*frame);
  std::unique_ptr<TransformableVideoFrameInterface> clone =
      CloneSenderVideoFrame(&video_frame);

  EXPECT_EQ(clone->IsKeyFrame(), video_frame.IsKeyFrame());
  EXPECT_EQ(clone->GetPayloadType(), video_frame.GetPayloadType());
  EXPECT_EQ(clone->GetMimeType(), video_frame.GetMimeType());
  EXPECT_EQ(clone->GetSsrc(), video_frame.GetSsrc());
  EXPECT_TRUE(std::holds_alternative<RtpTimestampWithOffset>(
      clone->GetRtpTimestampInfo()));
  EXPECT_TRUE(std::holds_alternative<RtpTimestampWithOffset>(
      video_frame.GetRtpTimestampInfo()));
  EXPECT_EQ(
      std::get<RtpTimestampWithOffset>(clone->GetRtpTimestampInfo()),
      std::get<RtpTimestampWithOffset>(video_frame.GetRtpTimestampInfo()));
  EXPECT_EQ(clone->Metadata(), video_frame.Metadata());
  EXPECT_EQ(clone->Rid(), video_frame.Rid());
}

TEST_F(RtpSenderVideoFrameTransformerDelegateTest, MetadataAfterSetMetadata) {
  auto delegate = make_ref_counted<RTPSenderVideoFrameTransformerDelegate>(
      &test_sender_, frame_transformer_,
      /*ssrc=*/1111, /*rid=*/"rid",
      time_controller_.CreateTaskQueueFactory().get());

  std::unique_ptr<TransformableFrameInterface> frame =
      GetTransformableFrame(delegate);
  ASSERT_TRUE(frame);
  auto& video_frame = static_cast<TransformableVideoFrameInterface&>(*frame);

  VideoFrameMetadata metadata;
  metadata.SetFrameType(VideoFrameType::kVideoFrameKey);
  metadata.SetFrameId(654);
  metadata.SetSsrc(2222);
  metadata.SetCsrcs({1, 2, 3});

  video_frame.SetMetadata(metadata);
  VideoFrameMetadata actual_metadata = video_frame.Metadata();

  // TODO(bugs.webrtc.org/14708): Just EXPECT_EQ the whole Metadata once the
  // equality operator lands.
  EXPECT_EQ(metadata.GetFrameType(), actual_metadata.GetFrameType());
  EXPECT_EQ(metadata.GetFrameId(), actual_metadata.GetFrameId());
  EXPECT_EQ(metadata.GetSsrc(), actual_metadata.GetSsrc());
  EXPECT_EQ(metadata.GetCsrcs(), actual_metadata.GetCsrcs());
}

TEST_F(RtpSenderVideoFrameTransformerDelegateTest,
       ReceiverFrameConvertedToSenderFrame) {
  auto delegate = make_ref_counted<RTPSenderVideoFrameTransformerDelegate>(
      &test_sender_, frame_transformer_,
      /*ssrc=*/1111, /*rid=*/"rid",
      time_controller_.CreateTaskQueueFactory().get());

  const uint8_t payload_type = 1;
  const RtpTimestampInfo timestamp = RtpTimestampWithOffset{2};
  const std::vector<uint32_t> frame_csrcs = {123, 456, 789};
  const std::array<uint8_t, 3> buffer = {3, 2, 1};

  auto mock_receiver_frame =
      std::make_unique<NiceMock<MockTransformableVideoFrame>>();
  ON_CALL(*mock_receiver_frame, GetDirection)
      .WillByDefault(Return(TransformableFrameInterface::Direction::kReceiver));
  VideoFrameMetadata metadata;
  metadata.SetCodec(kVideoCodecVP8);
  metadata.SetRTPVideoHeaderCodecSpecifics(RTPVideoHeaderVP8());
  metadata.SetCsrcs(frame_csrcs);
  ON_CALL(*mock_receiver_frame, Metadata).WillByDefault(Return(metadata));
  ON_CALL(*mock_receiver_frame, GetData).WillByDefault(Return(buffer));
  ON_CALL(*mock_receiver_frame, GetPayloadType)
      .WillByDefault(Return(payload_type));
  ON_CALL(*mock_receiver_frame, GetRtpTimestampInfo)
      .WillByDefault(Return(timestamp));

  scoped_refptr<TransformedFrameCallback> callback;
  EXPECT_CALL(*frame_transformer_, RegisterTransformedFrameSinkCallback)
      .WillOnce(SaveArg<0>(&callback));
  delegate->Init();
  ASSERT_TRUE(callback);

  Event event;
  EXPECT_CALL(
      test_sender_,
      SendVideoFrame(payload_type, kVideoCodecVP8, timestamp,
                     /*capture_time=*/Timestamp::MinusInfinity(),
                     ElementsAreArray(buffer), _, _,
                     /*expected_retransmission_time=*/TimeDelta::Millis(10),
                     frame_csrcs))
      .WillOnce(WithoutArgs([&] {
        event.Set();
        return true;
      }));

  callback->OnTransformedFrame(std::move(mock_receiver_frame));

  event.Wait(TimeDelta::Seconds(1));
}

TEST_F(RtpSenderVideoFrameTransformerDelegateTest, SettingRTPTimestamp) {
  auto delegate = make_ref_counted<RTPSenderVideoFrameTransformerDelegate>(
      &test_sender_, frame_transformer_,
      /*ssrc=*/1111, /*rid=*/"rid",
      time_controller_.CreateTaskQueueFactory().get());

  std::unique_ptr<TransformableFrameInterface> frame =
      GetTransformableFrame(delegate);
  ASSERT_TRUE(frame);
  auto& video_frame = static_cast<TransformableVideoFrameInterface&>(*frame);

  uint32_t rtp_timestamp = 12345;
  ASSERT_TRUE(std::holds_alternative<RtpTimestampWithOffset>(
      video_frame.GetRtpTimestampInfo()));
  ASSERT_FALSE(std::get<RtpTimestampWithOffset>(
                   video_frame.GetRtpTimestampInfo()) == rtp_timestamp);

  video_frame.SetRTPTimestamp(rtp_timestamp);
  EXPECT_TRUE(std::holds_alternative<RtpTimestampWithOffset>(
      video_frame.GetRtpTimestampInfo()));
  EXPECT_EQ(std::get<RtpTimestampWithOffset>(video_frame.GetRtpTimestampInfo()),
            rtp_timestamp);
}

TEST_F(RtpSenderVideoFrameTransformerDelegateTest,
       ShortCircuitingSkipsTransform) {
  auto delegate = make_ref_counted<RTPSenderVideoFrameTransformerDelegate>(
      &test_sender_, frame_transformer_,
      /*ssrc=*/1111, /*rid=*/"rid",
      time_controller_.CreateTaskQueueFactory().get());
  EXPECT_CALL(*frame_transformer_,
              RegisterTransformedFrameSinkCallback(_, 1111));
  delegate->Init();

  delegate->StartShortCircuiting();

  std::vector<uint32_t> csrcs = {1, 2, 3};
  // Will not call the actual transformer.
  EXPECT_CALL(*frame_transformer_, Transform).Times(0);
  // Will pass the frame straight to the reciever.
  EXPECT_CALL(test_sender_,
              SendVideoFrame(_, _, _, _, _, _, _, _, ElementsAreArray(csrcs)));

  EncodedImage encoded_image;
  encoded_image.SetEncodedData(EncodedImageBuffer::Create(1));
  delegate->TransformFrame(
      /*payload_type=*/1, VideoCodecType::kVideoCodecVP8, /*rtp_timestamp=*/2,
      encoded_image, RTPVideoHeader(),
      /*expected_retransmission_time=*/TimeDelta::Millis(10), csrcs);
}

TEST_F(RtpSenderVideoFrameTransformerDelegateTest,
       StopShortCircuitingResumesTransform) {
  auto delegate = make_ref_counted<RTPSenderVideoFrameTransformerDelegate>(
      &test_sender_, frame_transformer_,
      /*ssrc=*/1111, /*rid=*/"rid",
      time_controller_.CreateTaskQueueFactory().get());
  EXPECT_CALL(*frame_transformer_,
              RegisterTransformedFrameSinkCallback(_, 1111));
  delegate->Init();

  InSequence s;
  // While short circuiting, the frame is passed straight to the sender.
  EXPECT_CALL(test_sender_, SendVideoFrame);
  // Once short circuiting stops, the frame is passed to the transformer again.
  EXPECT_CALL(*frame_transformer_, Transform);

  EncodedImage encoded_image;
  encoded_image.SetEncodedData(EncodedImageBuffer::Create(1));
  delegate->StartShortCircuiting();
  delegate->TransformFrame(
      /*payload_type=*/1, VideoCodecType::kVideoCodecVP8, /*rtp_timestamp=*/2,
      encoded_image, RTPVideoHeader(),
      /*expected_retransmission_time=*/TimeDelta::Millis(10));

  delegate->StopShortCircuiting();
  delegate->TransformFrame(
      /*payload_type=*/1, VideoCodecType::kVideoCodecVP8, /*rtp_timestamp=*/3,
      encoded_image, RTPVideoHeader(),
      /*expected_retransmission_time=*/TimeDelta::Millis(10));
}

TEST_F(RtpSenderVideoFrameTransformerDelegateTest, CreateSenderVideoFrame) {
  const uint8_t payload_data[] = {1, 2, 3};
  std::vector<uint32_t> csrcs = {1, 2};
  RTPVideoHeader video_header;
  video_header.codec = VideoCodecType::kVideoCodecVP8;
  RTPVideoHeaderVP8 vp8;
  vp8.InitRTPVideoHeaderVP8();
  video_header.video_type_header = vp8;

  auto encoded_image_buffer =
      EncodedImageBuffer::Create(payload_data, sizeof(payload_data));
  EncodedImage encoded_image;
  encoded_image.SetEncodedData(std::move(encoded_image_buffer));
  encoded_image.set_frame_type(VideoFrameType::kVideoFrameKey);
  encoded_image.capture_time_ms_ = 123;
  encoded_image.SetPresentationTimestamp(Timestamp::Micros(7890));

  auto frame = CreateSenderVideoFrame(
      encoded_image, video_header, /*payload_type=*/99,
      VideoCodecType::kVideoCodecVP8, RtpTimestampWithOffset{456}, csrcs);

  ASSERT_TRUE(frame);
  EXPECT_TRUE(frame->IsKeyFrame());
  EXPECT_EQ(frame->CaptureTime(), Timestamp::Millis(123));
  EXPECT_THAT(frame->GetData(), ElementsAreArray(payload_data));
  EXPECT_EQ(frame->GetPayloadType(), 99);
  EXPECT_EQ(frame->GetSsrc(), 0u);
  EXPECT_THAT(frame->Metadata().GetCsrcs(), ElementsAreArray(csrcs));
  EXPECT_EQ(frame->Rid(), "");
  EXPECT_EQ(frame->GetPresentationTimestamp(), Timestamp::Micros(7890));
  auto rtp_timestamp_info = frame->GetRtpTimestampInfo();
  ASSERT_TRUE(
      std::holds_alternative<RtpTimestampWithOffset>(rtp_timestamp_info));
  EXPECT_EQ(std::get<RtpTimestampWithOffset>(rtp_timestamp_info), 456u);
}

}  // namespace
}  // namespace webrtc
