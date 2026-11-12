/*
 * Copyright 2025 The WebRTC project authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file in the root of the source
 * tree. An additional intellectual property rights grant can be found
 * in the file PATENTS.  All contributing project authors may
 * be found in the AUTHORS file in the root of the source tree.
 */

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/testsupport/file_utils.h"
#include "test/testsupport/frame_reader.h"
#include "video/corruption_detection/evaluation/utils.h"

namespace webrtc {
namespace {

using ::testing::HasSubstr;
using ::testing::Not;

// Dimension for the test frames.
constexpr int kWidth = 4;
constexpr int kHeight = 4;
constexpr int kChromaWidth = 2;
constexpr int kChromaHeight = 2;
constexpr int kFrameRate = 30;

// An arbitrary 4x4 raw YUV420 frame.
constexpr uint8_t kFrameYContent[kWidth * kHeight] = {
    12,  5,   7,   11,  //
    159, 15,  11,  0,   //
    4,   240, 131, 59,  //
    61,  87,  11,  0    //
};
constexpr uint8_t kFrameUContent[kChromaWidth * kChromaHeight] = {
    248, 184,  //
    139, 229   //
};
constexpr uint8_t kFrameVContent[kChromaWidth * kChromaHeight] = {
    32, 69,  //
    7, 193   //
};

// Concatenates and returns Y, U and V content into a single vector.
std::vector<uint8_t> ConcatenateVideoChannels() {
  std::vector<uint8_t> file_content_flattened;
  file_content_flattened.insert(file_content_flattened.end(), kFrameYContent,
                                kFrameYContent + kWidth * kHeight);
  file_content_flattened.insert(file_content_flattened.end(), kFrameUContent,
                                kFrameUContent + kChromaWidth * kChromaHeight);
  file_content_flattened.insert(file_content_flattened.end(), kFrameVContent,
                                kFrameVContent + kChromaWidth * kChromaHeight);
  return file_content_flattened;
}

TEST(TempY4mFileCreatorTest, CheckIfY4mFileIsCreated) {
  std::vector<uint8_t> file_content_flattened = ConcatenateVideoChannels();

  TempY4mFileCreator temp_y4m_file_creator(kWidth, kHeight, kFrameRate);
  temp_y4m_file_creator.CreateTempY4mFile(file_content_flattened);

  EXPECT_TRUE(test::FileExists(temp_y4m_file_creator.y4m_filepath()));
}

TEST(TempY4mFileCreatorTest, CanCreateMultipleFiles) {
  std::vector<uint8_t> file_content_flattened = ConcatenateVideoChannels();

  TempY4mFileCreator temp_y4m_file_creator_1(kWidth, kHeight, kFrameRate);
  TempY4mFileCreator temp_y4m_file_creator_2(kWidth, kHeight, kFrameRate);
  temp_y4m_file_creator_1.CreateTempY4mFile(file_content_flattened);
  temp_y4m_file_creator_2.CreateTempY4mFile(file_content_flattened);

  // Check that the files are created.
  ASSERT_TRUE(test::FileExists(temp_y4m_file_creator_1.y4m_filepath()));
  ASSERT_TRUE(test::FileExists(temp_y4m_file_creator_2.y4m_filepath()));
  // Check that the created files have different paths.
  EXPECT_THAT(temp_y4m_file_creator_1.y4m_filepath(),
              Not(temp_y4m_file_creator_2.y4m_filepath()));
}

TEST(TempY4mFileCreatorTest, CheckIfY4mFileHasCorrectContent) {
  std::vector<uint8_t> file_content_flattened = ConcatenateVideoChannels();

  TempY4mFileCreator temp_y4m_file_creator(kWidth, kHeight, kFrameRate);
  temp_y4m_file_creator.CreateTempY4mFile(file_content_flattened);
  std::unique_ptr<test::FrameReader> frame_generator =
      CreateY4mFrameReader(std::string(temp_y4m_file_creator.y4m_filepath()),
                           test::YuvFrameReaderImpl::RepeatMode::kSingle);

  scoped_refptr<I420Buffer> i420_buffer = frame_generator->PullFrame();
  ASSERT_EQ(i420_buffer->width(), kWidth);
  ASSERT_EQ(i420_buffer->height(), kHeight);
  for (int i = 0; i < kWidth * kHeight; ++i) {
    EXPECT_EQ(i420_buffer->DataY()[i], kFrameYContent[i]);
  }
  for (int i = 0; i < kChromaWidth * kChromaHeight; ++i) {
    EXPECT_EQ(i420_buffer->DataU()[i], kFrameUContent[i]);
    EXPECT_EQ(i420_buffer->DataV()[i], kFrameVContent[i]);
  }
  // No more frames.
  EXPECT_THAT(frame_generator->PullFrame(), nullptr);
}

#if GTEST_HAS_DEATH_TEST
TEST(TempY4mFileCreatorTest, ContentEndInMiddleError) {
  std::vector<uint8_t> file_content_flattened = ConcatenateVideoChannels();
  std::vector<uint8_t> only_luma_content(
      file_content_flattened.begin(),
      file_content_flattened.begin() + kWidth * kHeight);

  TempY4mFileCreator temp_y4m_file_creator(kWidth, kHeight, kFrameRate);
  EXPECT_DEATH(temp_y4m_file_creator.CreateTempY4mFile(only_luma_content),
               HasSubstr("Content size is not a multiple of frame size."));
}
#endif  // GTEST_HAS_DEATH_TEST

TEST(ReadMetadataFromY4mHeaderTest, ReadMetadataFromY4mHeader) {
  std::vector<uint8_t> file_content_flattened = ConcatenateVideoChannels();

  TempY4mFileCreator temp_y4m_file_creator(kWidth, kHeight, kFrameRate);
  temp_y4m_file_creator.CreateTempY4mFile(file_content_flattened);
  Y4mMetadata y4m_metadata =
      ReadMetadataFromY4mHeader(temp_y4m_file_creator.y4m_filepath());

  EXPECT_EQ(y4m_metadata.width, kWidth);
  EXPECT_EQ(y4m_metadata.height, kHeight);
  EXPECT_EQ(y4m_metadata.framerate, kFrameRate);
}

}  // namespace
}  // namespace webrtc
