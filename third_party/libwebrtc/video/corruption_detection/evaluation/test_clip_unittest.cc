/*
 * Copyright 2025 The WebRTC project authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file in the root of the source
 * tree. An additional intellectual property rights grant can be found
 * in the file PATENTS.  All contributing project authors may
 * be found in the AUTHORS file in the root of the source tree.
 */

#include "video/corruption_detection/evaluation/test_clip.h"

#include <cstdint>
#include <cstdio>
#include <string>

#include "absl/strings/string_view.h"
#include "api/video_codecs/video_codec.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/testsupport/file_utils.h"
#include "video/corruption_detection/evaluation/utils.h"

namespace webrtc {
namespace {

using ::testing::HasSubstr;

constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr int kFramerate = 50;
constexpr VideoCodecMode kCodecMode = VideoCodecMode::kRealtimeVideo;

constexpr int kDummyVideoWidth = 2;
constexpr int kDummyVideoHeight = 2;
// One frame of dummy video.
constexpr uint8_t kDummyFileContent[kDummyVideoWidth * kDummyVideoHeight * 3 /
                                    2] = {0, 1, 2, 3, 4, 5};

#if GTEST_HAS_DEATH_TEST
TEST(TestClipTest, FileDoesNotExist) {
  EXPECT_DEATH(TestClip::CreateY4mClip("does_not_exist", kCodecMode),
               HasSubstr("Could not find clip does_not_exist"));
  EXPECT_DEATH(TestClip::CreateY4mClip("does_not_exist.y4m", kCodecMode),
               HasSubstr("Could not find clip does_not_exist"));
  EXPECT_DEATH(TestClip::CreateYuvClip("does_not_exist", kWidth, kHeight,
                                       kFramerate, kCodecMode),
               HasSubstr("Could not find clip does_not_exist"));
  EXPECT_DEATH(TestClip::CreateYuvClip("does_not_exist.yuv", kWidth, kHeight,
                                       kFramerate, kCodecMode),
               HasSubstr("Could not find clip does_not_exist"));
}
#endif  // GTEST_HAS_DEATH_TEST

TEST(TestClipTest, CreateYuvClipWithoutExtension) {
  const std::string kFilenameYuvWithoutExtension =
      "ConferenceMotion_1280_720_50";
  const std::string kFilenameYuvWithExtension =
      "ConferenceMotion_1280_720_50.yuv";

  TestClip kDefaultTestClip = TestClip::CreateYuvClip(
      kFilenameYuvWithoutExtension, kWidth, kHeight, kFramerate, kCodecMode);

  EXPECT_THAT(kDefaultTestClip.codec_mode(), kCodecMode);
  // Using `HasSubstr` because `ResourcePath` adds a prefix to the filename.
  EXPECT_THAT(kDefaultTestClip.clip_path(),
              HasSubstr(kFilenameYuvWithExtension));
  EXPECT_THAT(kDefaultTestClip.width(), kWidth);
  EXPECT_THAT(kDefaultTestClip.height(), kHeight);
  EXPECT_THAT(kDefaultTestClip.framerate(), kFramerate);
  EXPECT_THAT(kDefaultTestClip.is_yuv(), true);
}

TEST(TestClipTest, CreateYuvClipWithExtension) {
  // Create a YUV file and add dummy data to it. This will simulate that
  // `TestClip` can find a video when specified with full path.
  const std::string yuv_filepath =
      test::TempFilename(test::OutputPath(), "temp_video.yuv");
  FILE* video_file = fopen(yuv_filepath.c_str(), "wb");
  ASSERT_TRUE(video_file);
  fwrite(kDummyFileContent, 1, kDummyVideoWidth * kDummyVideoHeight * 3 / 2,
         video_file);
  // `fclose` returns 0 on success.
  ASSERT_EQ(fclose(video_file), 0);

  TestClip kDefaultTestClip =
      TestClip::CreateYuvClip(yuv_filepath, kDummyVideoWidth, kDummyVideoHeight,
                              kFramerate, kCodecMode);

  EXPECT_THAT(kDefaultTestClip.codec_mode(), kCodecMode);
  EXPECT_THAT(kDefaultTestClip.clip_path(), HasSubstr(yuv_filepath));
  EXPECT_THAT(kDefaultTestClip.width(), kDummyVideoWidth);
  EXPECT_THAT(kDefaultTestClip.height(), kDummyVideoHeight);
  EXPECT_THAT(kDefaultTestClip.framerate(), kFramerate);
  EXPECT_THAT(kDefaultTestClip.is_yuv(), true);
  EXPECT_TRUE(test::RemoveFile(yuv_filepath));
}

TEST(TestClipTest, CreateY4mClipWithExtension) {
  // Create a temporary Y4M file for testing. This will simulate that
  // `TestClip` can find a video when specified with full path.
  TempY4mFileCreator temp_y4m_file_creator(kDummyVideoWidth, kDummyVideoHeight,
                                           kFramerate);
  temp_y4m_file_creator.CreateTempY4mFile(kDummyFileContent);
  const absl::string_view y4m_filepath = temp_y4m_file_creator.y4m_filepath();

  TestClip kDefaultTestClip = TestClip::CreateY4mClip(y4m_filepath, kCodecMode);
  EXPECT_THAT(kDefaultTestClip.codec_mode(), kCodecMode);
  EXPECT_THAT(kDefaultTestClip.clip_path(), HasSubstr(y4m_filepath));
  EXPECT_THAT(kDefaultTestClip.width(), kDummyVideoWidth);
  EXPECT_THAT(kDefaultTestClip.height(), kDummyVideoHeight);
  EXPECT_THAT(kDefaultTestClip.framerate(), kFramerate);
  EXPECT_THAT(kDefaultTestClip.is_yuv(), false);
}

}  // namespace
}  // namespace webrtc
