/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/generic_decoder.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/types/optional.h"
#include "api/array_view.h"
#include "api/rtp_packet_infos.h"
#include "api/video_codecs/video_decoder.h"
#include "common_video/test/utilities.h"
#include "modules/video_coding/timing/timing.h"
#include "system_wrappers/include/clock.h"
#include "test/fake_decoder.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/scoped_key_value_config.h"
#include "test/time_controller/simulated_time_controller.h"

namespace webrtc {
namespace video_coding {

class ReceiveCallback : public VCMReceiveCallback {
 public:
  int32_t FrameToRender(VideoFrame& frame,
                        absl::optional<uint8_t> qp,
                        TimeDelta decode_time,
                        VideoContentType content_type,
                        VideoFrameType frame_type) override {
    frames_.push_back(frame);
    return 0;
  }

  absl::optional<VideoFrame> PopLastFrame() {
    if (frames_.empty())
      return absl::nullopt;
    auto ret = frames_.front();
    frames_.pop_back();
    return ret;
  }

  rtc::ArrayView<const VideoFrame> GetAllFrames() const { return frames_; }

  void OnDroppedFrames(uint32_t frames_dropped) {
    frames_dropped_ += frames_dropped;
  }

  uint32_t frames_dropped() const { return frames_dropped_; }

 private:
  std::vector<VideoFrame> frames_;
  uint32_t frames_dropped_ = 0;
};

class GenericDecoderTest : public ::testing::Test {
 protected:
  GenericDecoderTest()
      : time_controller_(Timestamp::Zero()),
        clock_(time_controller_.GetClock()),
        timing_(time_controller_.GetClock(), field_trials_),
        decoder_(time_controller_.GetTaskQueueFactory()),
        vcm_callback_(&timing_, time_controller_.GetClock(), field_trials_),
        generic_decoder_(&decoder_) {}

  void SetUp() override {
    generic_decoder_.RegisterDecodeCompleteCallback(&vcm_callback_);
    vcm_callback_.SetUserReceiveCallback(&user_callback_);
    VideoDecoder::Settings settings;
    settings.set_codec_type(kVideoCodecVP8);
    settings.set_max_render_resolution({10, 10});
    settings.set_number_of_cores(4);
    generic_decoder_.Configure(settings);
  }

  GlobalSimulatedTimeController time_controller_;
  Clock* const clock_;
  test::ScopedKeyValueConfig field_trials_;
  VCMTiming timing_;
  webrtc::test::FakeDecoder decoder_;
  VCMDecodedFrameCallback vcm_callback_;
  VCMGenericDecoder generic_decoder_;
  ReceiveCallback user_callback_;
};

TEST_F(GenericDecoderTest, PassesPacketInfos) {
  RtpPacketInfos packet_infos = CreatePacketInfos(3);
  EncodedFrame encoded_frame;
  encoded_frame.SetPacketInfos(packet_infos);
  generic_decoder_.Decode(encoded_frame, clock_->CurrentTime());
  time_controller_.AdvanceTime(TimeDelta::Millis(10));
  absl::optional<VideoFrame> decoded_frame = user_callback_.PopLastFrame();
  ASSERT_TRUE(decoded_frame.has_value());
  EXPECT_EQ(decoded_frame->packet_infos().size(), 3U);
}

TEST_F(GenericDecoderTest, FrameDroppedIfTooManyFramesInFlight) {
  constexpr int kMaxFramesInFlight = 10;
  decoder_.SetDelayedDecoding(10);
  for (int i = 0; i < kMaxFramesInFlight + 1; ++i) {
    EncodedFrame encoded_frame;
    encoded_frame.SetRtpTimestamp(90000 * i);
    generic_decoder_.Decode(encoded_frame, clock_->CurrentTime());
  }

  time_controller_.AdvanceTime(TimeDelta::Millis(10));

  auto frames = user_callback_.GetAllFrames();
  ASSERT_EQ(10U, frames.size());
  // Expect that the first frame was dropped since all decodes released at the
  // same time and the oldest frame info is the first one dropped.
  EXPECT_EQ(frames[0].rtp_timestamp(), 90000u);
  EXPECT_EQ(1u, user_callback_.frames_dropped());
}

TEST_F(GenericDecoderTest, PassesPacketInfosForDelayedDecoders) {
  RtpPacketInfos packet_infos = CreatePacketInfos(3);
  decoder_.SetDelayedDecoding(100);

  {
    // Ensure the original frame is destroyed before the decoding is completed.
    EncodedFrame encoded_frame;
    encoded_frame.SetPacketInfos(packet_infos);
    generic_decoder_.Decode(encoded_frame, clock_->CurrentTime());
  }

  time_controller_.AdvanceTime(TimeDelta::Millis(200));
  absl::optional<VideoFrame> decoded_frame = user_callback_.PopLastFrame();
  ASSERT_TRUE(decoded_frame.has_value());
  EXPECT_EQ(decoded_frame->packet_infos().size(), 3U);
}

TEST_F(GenericDecoderTest, MaxCompositionDelayNotSetByDefault) {
  EncodedFrame encoded_frame;
  generic_decoder_.Decode(encoded_frame, clock_->CurrentTime());
  time_controller_.AdvanceTime(TimeDelta::Millis(10));
  absl::optional<VideoFrame> decoded_frame = user_callback_.PopLastFrame();
  ASSERT_TRUE(decoded_frame.has_value());
  EXPECT_THAT(
      decoded_frame->render_parameters().max_composition_delay_in_frames,
      testing::Eq(absl::nullopt));
}

TEST_F(GenericDecoderTest, MaxCompositionDelayActivatedByPlayoutDelay) {
  EncodedFrame encoded_frame;
  // VideoReceiveStream2 would set MaxCompositionDelayInFrames if playout delay
  // is specified as X,Y, where X=0, Y>0.
  constexpr int kMaxCompositionDelayInFrames = 3;  // ~50 ms at 60 fps.
  timing_.SetMaxCompositionDelayInFrames(
      absl::make_optional(kMaxCompositionDelayInFrames));
  generic_decoder_.Decode(encoded_frame, clock_->CurrentTime());
  time_controller_.AdvanceTime(TimeDelta::Millis(10));
  absl::optional<VideoFrame> decoded_frame = user_callback_.PopLastFrame();
  ASSERT_TRUE(decoded_frame.has_value());
  EXPECT_THAT(
      decoded_frame->render_parameters().max_composition_delay_in_frames,
      testing::Optional(kMaxCompositionDelayInFrames));
}

TEST_F(GenericDecoderTest, IsLowLatencyStreamFalseByDefault) {
  EncodedFrame encoded_frame;
  generic_decoder_.Decode(encoded_frame, clock_->CurrentTime());
  time_controller_.AdvanceTime(TimeDelta::Millis(10));
  absl::optional<VideoFrame> decoded_frame = user_callback_.PopLastFrame();
  ASSERT_TRUE(decoded_frame.has_value());
  EXPECT_FALSE(decoded_frame->render_parameters().use_low_latency_rendering);
}

TEST_F(GenericDecoderTest, IsLowLatencyStreamActivatedByPlayoutDelay) {
  EncodedFrame encoded_frame;
  const VideoPlayoutDelay kPlayoutDelay(TimeDelta::Zero(),
                                        TimeDelta::Millis(50));
  timing_.set_min_playout_delay(kPlayoutDelay.min());
  timing_.set_max_playout_delay(kPlayoutDelay.max());
  generic_decoder_.Decode(encoded_frame, clock_->CurrentTime());
  time_controller_.AdvanceTime(TimeDelta::Millis(10));
  absl::optional<VideoFrame> decoded_frame = user_callback_.PopLastFrame();
  ASSERT_TRUE(decoded_frame.has_value());
  EXPECT_TRUE(decoded_frame->render_parameters().use_low_latency_rendering);
}

}  // namespace video_coding
}  // namespace webrtc
