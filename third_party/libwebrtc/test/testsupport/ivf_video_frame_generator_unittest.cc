/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/testsupport/ivf_video_frame_generator.h"

#include <memory>
#include <vector>

#include "absl/types/optional.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/test/create_frame_generator.h"
#include "api/units/time_delta.h"
#include "api/video/encoded_image.h"
#include "api/video/video_codec_type.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "media/base/codec.h"
#include "media/base/media_constants.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "modules/video_coding/codecs/vp9/include/vp9.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "modules/video_coding/utility/ivf_file_writer.h"
#include "rtc_base/event.h"
#include "test/gtest.h"
#include "test/testsupport/file_utils.h"
#include "test/video_codec_settings.h"

#if defined(WEBRTC_USE_H264)
#include "modules/video_coding/codecs/h264/include/h264.h"
#include "rtc_base/synchronization/mutex.h"

#endif

namespace webrtc {
namespace test {
namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr int kVideoFramesCount = 30;
constexpr int kMaxFramerate = 30;
constexpr TimeDelta kMaxFrameEncodeWaitTimeout = TimeDelta::Seconds(2);
static const VideoEncoder::Capabilities kCapabilities(false);

#if defined(WEBRTC_ANDROID) || defined(WEBRTC_IOS) || defined(WEBRTC_ARCH_ARM64)
constexpr double kExpectedMinPsnr = 35;
#else
constexpr double kExpectedMinPsnr = 39;
#endif

class IvfFileWriterEncodedCallback : public EncodedImageCallback {
 public:
  IvfFileWriterEncodedCallback(const std::string& file_name,
                               VideoCodecType video_codec_type,
                               int expected_frames_count)
      : file_writer_(
            IvfFileWriter::Wrap(FileWrapper::OpenWriteOnly(file_name), 0)),
        video_codec_type_(video_codec_type),
        expected_frames_count_(expected_frames_count) {
    EXPECT_TRUE(file_writer_.get());
  }
  ~IvfFileWriterEncodedCallback() { EXPECT_TRUE(file_writer_->Close()); }

  Result OnEncodedImage(const EncodedImage& encoded_image,
                        const CodecSpecificInfo* codec_specific_info) override {
    EXPECT_TRUE(file_writer_->WriteFrame(encoded_image, video_codec_type_));

    MutexLock lock(&lock_);
    received_frames_count_++;
    RTC_CHECK_LE(received_frames_count_, expected_frames_count_);
    if (received_frames_count_ == expected_frames_count_) {
      expected_frames_count_received_.Set();
    }
    return Result(Result::Error::OK);
  }

  bool WaitForExpectedFramesReceived(TimeDelta timeout) {
    return expected_frames_count_received_.Wait(timeout);
  }

 private:
  std::unique_ptr<IvfFileWriter> file_writer_;
  const VideoCodecType video_codec_type_;
  const int expected_frames_count_;

  Mutex lock_;
  int received_frames_count_ RTC_GUARDED_BY(lock_) = 0;
  rtc::Event expected_frames_count_received_;
};

class IvfVideoFrameGeneratorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    file_name_ =
        webrtc::test::TempFilename(webrtc::test::OutputPath(), "test_file.ivf");
  }
  void TearDown() override { webrtc::test::RemoveFile(file_name_); }

  VideoFrame BuildFrame(FrameGeneratorInterface::VideoFrameData frame_data) {
    return VideoFrame::Builder()
        .set_video_frame_buffer(frame_data.buffer)
        .set_update_rect(frame_data.update_rect)
        .build();
  }

  void CreateTestVideoFile(VideoCodecType video_codec_type,
                           std::unique_ptr<VideoEncoder> video_encoder) {
    std::unique_ptr<test::FrameGeneratorInterface> frame_generator =
        test::CreateSquareFrameGenerator(
            kWidth, kHeight, test::FrameGeneratorInterface::OutputType::kI420,
            absl::nullopt);

    VideoCodec codec_settings;
    webrtc::test::CodecSettings(video_codec_type, &codec_settings);
    codec_settings.width = kWidth;
    codec_settings.height = kHeight;
    codec_settings.maxFramerate = kMaxFramerate;
    const uint32_t kBitrateBps = 500000;
    VideoBitrateAllocation bitrate_allocation;
    bitrate_allocation.SetBitrate(0, 0, kBitrateBps);

    IvfFileWriterEncodedCallback ivf_writer_callback(
        file_name_, video_codec_type, kVideoFramesCount);

    video_encoder->RegisterEncodeCompleteCallback(&ivf_writer_callback);
    video_encoder->SetRates(VideoEncoder::RateControlParameters(
        bitrate_allocation, static_cast<double>(codec_settings.maxFramerate)));
    ASSERT_EQ(WEBRTC_VIDEO_CODEC_OK,
              video_encoder->InitEncode(
                  &codec_settings,
                  VideoEncoder::Settings(kCapabilities, /*number_of_cores=*/1,
                                         /*max_payload_size=*/0)));

    uint32_t last_frame_timestamp = 0;

    for (int i = 0; i < kVideoFramesCount; ++i) {
      VideoFrame frame = BuildFrame(frame_generator->NextFrame());
      const uint32_t timestamp =
          last_frame_timestamp +
          kVideoPayloadTypeFrequency / codec_settings.maxFramerate;
      frame.set_rtp_timestamp(timestamp);

      last_frame_timestamp = timestamp;

      ASSERT_EQ(WEBRTC_VIDEO_CODEC_OK, video_encoder->Encode(frame, nullptr));
      video_frames_.push_back(frame);
    }

    ASSERT_TRUE(ivf_writer_callback.WaitForExpectedFramesReceived(
        kMaxFrameEncodeWaitTimeout));
  }

  Environment env_ = CreateEnvironment();
  std::string file_name_;
  std::vector<VideoFrame> video_frames_;
};

}  // namespace

TEST_F(IvfVideoFrameGeneratorTest, DoesNotKnowFps) {
  CreateTestVideoFile(VideoCodecType::kVideoCodecVP8, CreateVp8Encoder(env_));
  IvfVideoFrameGenerator generator(env_, file_name_);
  EXPECT_EQ(generator.fps(), absl::nullopt);
}

TEST_F(IvfVideoFrameGeneratorTest, Vp8) {
  CreateTestVideoFile(VideoCodecType::kVideoCodecVP8, CreateVp8Encoder(env_));
  IvfVideoFrameGenerator generator(env_, file_name_);
  for (size_t i = 0; i < video_frames_.size(); ++i) {
    auto& expected_frame = video_frames_[i];
    VideoFrame actual_frame = BuildFrame(generator.NextFrame());
    EXPECT_GT(I420PSNR(&expected_frame, &actual_frame), kExpectedMinPsnr);
  }
}

TEST_F(IvfVideoFrameGeneratorTest, Vp8DoubleRead) {
  CreateTestVideoFile(VideoCodecType::kVideoCodecVP8, CreateVp8Encoder(env_));
  IvfVideoFrameGenerator generator(env_, file_name_);
  for (size_t i = 0; i < video_frames_.size() * 2; ++i) {
    auto& expected_frame = video_frames_[i % video_frames_.size()];
    VideoFrame actual_frame = BuildFrame(generator.NextFrame());
    EXPECT_GT(I420PSNR(&expected_frame, &actual_frame), kExpectedMinPsnr);
  }
}

TEST_F(IvfVideoFrameGeneratorTest, Vp9) {
  CreateTestVideoFile(VideoCodecType::kVideoCodecVP9, CreateVp9Encoder(env_));
  IvfVideoFrameGenerator generator(env_, file_name_);
  for (size_t i = 0; i < video_frames_.size(); ++i) {
    auto& expected_frame = video_frames_[i];
    VideoFrame actual_frame = BuildFrame(generator.NextFrame());
    EXPECT_GT(I420PSNR(&expected_frame, &actual_frame), kExpectedMinPsnr);
  }
}

#if defined(WEBRTC_USE_H264)
TEST_F(IvfVideoFrameGeneratorTest, H264) {
  CreateTestVideoFile(VideoCodecType::kVideoCodecH264, CreateH264Encoder(env_));
  IvfVideoFrameGenerator generator(env_, file_name_);
  for (size_t i = 0; i < video_frames_.size(); ++i) {
    auto& expected_frame = video_frames_[i];
    VideoFrame actual_frame = BuildFrame(generator.NextFrame());
    EXPECT_GT(I420PSNR(&expected_frame, &actual_frame), kExpectedMinPsnr);
  }
}
#endif

}  // namespace test
}  // namespace webrtc
