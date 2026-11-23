/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/testsupport/ivf_frame_reader.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "api/environment/environment.h"
#include "api/scoped_refptr.h"
#include "api/test/create_frame_generator.h"
#include "api/test/frame_generator_interface.h"
#include "api/units/time_delta.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/resolution.h"
#include "api/video/video_bitrate_allocation.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/video_coding/codecs/av1/libaom_av1_encoder.h"
#include "modules/video_coding/codecs/h264/include/h264.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "modules/video_coding/codecs/vp9/include/vp9.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "modules/video_coding/utility/ivf_file_writer.h"
#include "rtc_base/checks.h"
#include "rtc_base/event.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/thread_annotations.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"
#include "test/testsupport/file_utils.h"
#include "test/testsupport/frame_reader.h"
#include "test/video_codec_settings.h"

namespace webrtc {
namespace test {
namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr int kVideoFramesCount = 10;
constexpr int kMaxFramerate = 30;
constexpr TimeDelta kMaxFrameEncodeWaitTimeout = TimeDelta::Seconds(2);

#if defined(WEBRTC_USE_H264)
constexpr bool kUseH264 = true;
#else
constexpr bool kUseH264 = false;
#endif

std::vector<VideoCodecType> GetCodecsToTest() {
  std::vector<VideoCodecType> codecs = {VideoCodecType::kVideoCodecVP8,
                                        VideoCodecType::kVideoCodecVP9,
                                        VideoCodecType::kVideoCodecAV1};
  if (kUseH264) {
    codecs.push_back(VideoCodecType::kVideoCodecH264);
  }
  return codecs;
}

class IvfFileWriterEncodedCallback : public EncodedImageCallback {
 public:
  IvfFileWriterEncodedCallback(const std::string& file_name,
                               VideoCodecType video_codec_type,
                               int expected_frames_count)
      : file_writer_(IvfFileWriter::Wrap(file_name, 0)),
        video_codec_type_(video_codec_type),
        expected_frames_count_(expected_frames_count) {
    EXPECT_TRUE(file_writer_.get());
  }
  ~IvfFileWriterEncodedCallback() override {
    EXPECT_TRUE(file_writer_->Close());
  }

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

  void OnFrameDropped(uint32_t /*rtp_timestamp*/,
                      int /*spatial_id*/,
                      bool /*is_end_of_temporal_unit*/) override {}

  bool WaitForExpectedFramesReceived(TimeDelta timeout) {
    return expected_frames_count_received_.Wait(timeout);
  }

 private:
  std::unique_ptr<IvfFileWriter> file_writer_;
  const VideoCodecType video_codec_type_;
  const int expected_frames_count_;

  Mutex lock_;
  int received_frames_count_ RTC_GUARDED_BY(lock_) = 0;
  Event expected_frames_count_received_;
};

class IvfFrameReaderTest : public ::testing::TestWithParam<VideoCodecType> {
 protected:
  void SetUp() override {
    file_name_ = test::TempFilename(test::OutputPath(), "test_file.ivf");
  }
  void TearDown() override { test::RemoveFile(file_name_); }

  std::unique_ptr<VideoEncoder> CreateEncoder(VideoCodecType codec_type) {
    switch (codec_type) {
      case VideoCodecType::kVideoCodecVP8:
        return CreateVp8Encoder(env_);
      case VideoCodecType::kVideoCodecVP9:
        return CreateVp9Encoder(env_);
      case VideoCodecType::kVideoCodecAV1:
        return CreateLibaomAv1Encoder(env_);
#if defined(WEBRTC_USE_H264)
      case VideoCodecType::kVideoCodecH264:
        return CreateH264Encoder(env_);
#endif
      default:
        RTC_CHECK(false) << "Unsupported codec type";
    }
  }

  void CreateTestVideoFile(VideoCodecType video_codec_type,
                           std::unique_ptr<VideoEncoder> video_encoder) {
    std::unique_ptr<test::FrameGeneratorInterface> frame_generator =
        test::CreateSquareFrameGenerator(
            kWidth, kHeight, test::FrameGeneratorInterface::OutputType::kI420,
            std::nullopt);

    VideoCodec codec_settings;
    test::CodecSettings(video_codec_type, &codec_settings);
    codec_settings.width = kWidth;
    codec_settings.height = kHeight;
    codec_settings.maxFramerate = kMaxFramerate;
    const uint32_t kBitrateBps = 500000;
    VideoBitrateAllocation bitrate_allocation;
    bitrate_allocation.SetBitrate(0, 0, kBitrateBps);

    IvfFileWriterEncodedCallback ivf_writer_callback(
        file_name_, video_codec_type, kVideoFramesCount);

    video_encoder->RegisterEncodeCompleteCallback(&ivf_writer_callback);
    ASSERT_EQ(
        WEBRTC_VIDEO_CODEC_OK,
        video_encoder->InitEncode(
            &codec_settings, VideoEncoder::Settings(/*number_of_cores=*/1,
                                                    /*max_payload_size=*/0)));
    video_encoder->SetRates(VideoEncoder::RateControlParameters(
        bitrate_allocation, static_cast<double>(codec_settings.maxFramerate)));

    uint32_t last_frame_timestamp = 0;

    for (int i = 0; i < kVideoFramesCount; ++i) {
      FrameGeneratorInterface::VideoFrameData frame_data =
          frame_generator->NextFrame();
      VideoFrame frame = VideoFrame::Builder()
                             .set_video_frame_buffer(frame_data.buffer)
                             .set_update_rect(frame_data.update_rect)
                             .build();
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

  Environment env_ = CreateTestEnvironment();
  std::string file_name_;
  std::vector<VideoFrame> video_frames_;
};

}  // namespace

TEST_P(IvfFrameReaderTest, ReadsAllFrames) {
  VideoCodecType codec_type = GetParam();
  CreateTestVideoFile(codec_type, CreateEncoder(codec_type));
  IvfFrameReader reader(env_, file_name_, /*repeat=*/false);

  EXPECT_EQ(reader.num_frames(), kVideoFramesCount);

  for (int i = 0; i < kVideoFramesCount; ++i) {
    int frame_num;
    scoped_refptr<I420Buffer> frame = reader.PullFrame(&frame_num);
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame_num, i);
    EXPECT_EQ(frame->width(), kWidth);
    EXPECT_EQ(frame->height(), kHeight);
  }

  // After all frames read, PullFrame should return nullptr.
  int frame_num;
  EXPECT_FALSE(reader.PullFrame(&frame_num));
}

TEST_P(IvfFrameReaderTest, ScalesOutput) {
  VideoCodecType codec_type = GetParam();
  CreateTestVideoFile(codec_type, CreateEncoder(codec_type));
  IvfFrameReader reader(env_, file_name_, /*repeat=*/false);

  int frame_num;
  Resolution target_resolution = {.width = kWidth * 2, .height = kHeight / 2};
  scoped_refptr<I420Buffer> frame = reader.PullFrame(
      &frame_num, target_resolution, FrameReader::Ratio({.num = 1, .den = 1}));

  ASSERT_TRUE(frame);
  EXPECT_EQ(frame_num, 0);
  EXPECT_EQ(frame->width(), kWidth * 2);
  EXPECT_EQ(frame->height(), kHeight / 2);
}

TEST_P(IvfFrameReaderTest, RepeatsFrames) {
  VideoCodecType codec_type = GetParam();
  CreateTestVideoFile(codec_type, CreateEncoder(codec_type));
  IvfFrameReader reader(env_, file_name_, /*repeat=*/true);

  EXPECT_EQ(reader.num_frames(), kVideoFramesCount);

  for (int i = 0; i < kVideoFramesCount; ++i) {
    int frame_num;
    scoped_refptr<I420Buffer> frame = reader.PullFrame(&frame_num);
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame_num, i);
  }

  int frame_num;
  scoped_refptr<I420Buffer> frame = reader.PullFrame(&frame_num);
  ASSERT_TRUE(frame);
  EXPECT_EQ(frame_num, kVideoFramesCount);
}

TEST_P(IvfFrameReaderTest, ScalesFramerateDown) {
  VideoCodecType codec_type = GetParam();
  CreateTestVideoFile(codec_type, CreateEncoder(codec_type));

  IvfFrameReader reader(env_, file_name_, /*repeat=*/false);
  int count = 0;
  int frame_num;
  while (reader.PullFrame(&frame_num, {.width = kWidth, .height = kHeight},
                          FrameReader::Ratio({.num = 1, .den = 2}))) {
    EXPECT_EQ(frame_num, count);
    count++;
  }
  EXPECT_EQ(count, kVideoFramesCount / 2);
}

TEST_P(IvfFrameReaderTest, ScalesFramerateUp) {
  VideoCodecType codec_type = GetParam();
  CreateTestVideoFile(codec_type, CreateEncoder(codec_type));

  IvfFrameReader reader(env_, file_name_, /*repeat=*/false);
  int count = 0;
  int frame_num;
  while (reader.PullFrame(&frame_num, {.width = kWidth, .height = kHeight},
                          FrameReader::Ratio({.num = 2, .den = 1}))) {
    EXPECT_EQ(frame_num, count);
    count++;
  }
  EXPECT_EQ(count, kVideoFramesCount * 2);
}

INSTANTIATE_TEST_SUITE_P(AllCodecs,
                         IvfFrameReaderTest,
                         ::testing::ValuesIn(GetCodecsToTest()));

}  // namespace test
}  // namespace webrtc
