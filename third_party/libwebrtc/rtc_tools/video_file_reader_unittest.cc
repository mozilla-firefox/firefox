/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_tools/video_file_reader.h"

#include <stdio.h>

#include <cstdio>
#include <string>

#include "api/scoped_refptr.h"
#include "api/video/video_frame_buffer.h"
#include "test/gtest.h"
#include "test/testsupport/file_utils.h"

namespace webrtc {
namespace test {

class Y4mFileReaderTest : public ::testing::Test {
 public:
  void SetUp() override {
    filename_ = TempFilename(test::OutputPath(), "test_video_file.y4m");

    // Create simple test video of size 6x4.
    FILE* file = fopen(filename_.c_str(), "wb");
    ASSERT_TRUE(file != nullptr);
    fprintf(file, "YUV4MPEG2 W6 H4 F57:10 C420 dummyParam\n");
    fprintf(file, "FRAME\n");

    const int width = 6;
    const int height = 4;
    const int i40_size = width * height * 3 / 2;
    // First frame.
    for (int i = 0; i < i40_size; ++i)
      fputc(static_cast<char>(i), file);
    fprintf(file, "FRAME\n");
    // Second frame.
    for (int i = 0; i < i40_size; ++i)
      fputc(static_cast<char>(i + i40_size), file);
    fclose(file);

    // Open the newly created file.
    video = test::OpenY4mFile(filename_);
    ASSERT_TRUE(video);
  }

  void TearDown() override {
    video = nullptr;
    EXPECT_TRUE(RemoveFile(filename_));
  }

  std::string filename_;
  scoped_refptr<test::Video> video;
};

TEST_F(Y4mFileReaderTest, TestParsingFileHeader) {
  EXPECT_EQ(6, video->width());
  EXPECT_EQ(4, video->height());
}

TEST_F(Y4mFileReaderTest, TestParsingNumberOfFrames) {
  EXPECT_EQ(2u, video->number_of_frames());
}

TEST_F(Y4mFileReaderTest, TestPixelContent) {
  int cnt = 0;
  for (const scoped_refptr<I420BufferInterface> frame : *video) {
    for (int i = 0; i < 6 * 4; ++i, ++cnt)
      EXPECT_EQ(cnt, frame->DataY()[i]);
    for (int i = 0; i < 3 * 2; ++i, ++cnt)
      EXPECT_EQ(cnt, frame->DataU()[i]);
    for (int i = 0; i < 3 * 2; ++i, ++cnt)
      EXPECT_EQ(cnt, frame->DataV()[i]);
  }
}

class YuvFileReaderTest : public ::testing::Test {
 public:
  void SetUp() override {
    filename_ = TempFilename(test::OutputPath(), "test_video_file.yuv");

    // Create simple test video of size 6x4.
    FILE* file = fopen(filename_.c_str(), "wb");
    ASSERT_TRUE(file != nullptr);

    const int width = 6;
    const int height = 4;
    const int i40_size = width * height * 3 / 2;
    // First frame.
    for (int i = 0; i < i40_size; ++i)
      fputc(static_cast<char>(i), file);
    // Second frame.
    for (int i = 0; i < i40_size; ++i)
      fputc(static_cast<char>(i + i40_size), file);
    fclose(file);

    // Open the newly created file.
    video = test::OpenYuvFile(filename_, 6, 4);
    ASSERT_TRUE(video);
  }

  void TearDown() override {
    video = nullptr;
    EXPECT_TRUE(RemoveFile(filename_));
  }

  std::string filename_;
  scoped_refptr<test::Video> video;
};

TEST_F(YuvFileReaderTest, TestParsingFileHeader) {
  EXPECT_EQ(6, video->width());
  EXPECT_EQ(4, video->height());
}

TEST_F(YuvFileReaderTest, TestParsingNumberOfFrames) {
  EXPECT_EQ(2u, video->number_of_frames());
}

TEST_F(YuvFileReaderTest, TestPixelContent) {
  int cnt = 0;
  for (const scoped_refptr<I420BufferInterface> frame : *video) {
    for (int i = 0; i < 6 * 4; ++i, ++cnt)
      EXPECT_EQ(cnt, frame->DataY()[i]);
    for (int i = 0; i < 3 * 2; ++i, ++cnt)
      EXPECT_EQ(cnt, frame->DataU()[i]);
    for (int i = 0; i < 3 * 2; ++i, ++cnt)
      EXPECT_EQ(cnt, frame->DataV()[i]);
  }
}

TEST(VideoFileReaderTest, OpenY4mFileAbortsOnLargeHeader) {
  const std::string file_name =
      TempFilename(test::OutputPath(), "temp_large_header.y4m");

  FILE* file = fopen(file_name.c_str(), "wb");
  ASSERT_TRUE(file != nullptr);

  fprintf(file, "YUV4MPEG2 ");

  // Write 2000 'A's without a newline (Simulate the attack)
  for (int i = 0; i < 2000; ++i) {
    fputc('A', file);
  }
  fclose(file);
  EXPECT_EQ(nullptr, OpenY4mFile(file_name));

  remove(file_name.c_str());
}
}  // namespace test
}  // namespace webrtc
