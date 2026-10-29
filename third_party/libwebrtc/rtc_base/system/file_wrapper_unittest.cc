/*
 *  Copyright 2021 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/system/file_wrapper.h"

#include <stdio.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "test/gtest.h"
#include "test/testsupport/file_utils.h"

namespace webrtc {

TEST(FileWrapper, FileSize) {
  auto test_info = ::testing::UnitTest::GetInstance()->current_test_info();
  std::string test_name =
      std::string(test_info->test_case_name()) + "_" + test_info->name();
  std::replace(test_name.begin(), test_name.end(), '/', '_');
  const std::string temp_dir = test::OutputPathWithRandomDirectory();
  const std::string temp_filename = temp_dir + test_name;

  // Write
  {
    FileWrapper file = FileWrapper::OpenWriteOnly(temp_filename);
    ASSERT_TRUE(file.is_open());
    EXPECT_EQ(file.FileSize(), 0);

    EXPECT_TRUE(file.Write("foo", 3));
    EXPECT_EQ(file.FileSize(), 3);

    // FileSize() doesn't change the file size.
    EXPECT_EQ(file.FileSize(), 3);

    // FileSize() doesn't move the write position.
    EXPECT_TRUE(file.Write("bar", 3));
    EXPECT_EQ(file.FileSize(), 6);
  }

  // Read
  {
    FileWrapper file = FileWrapper::OpenReadOnly(temp_filename);
    ASSERT_TRUE(file.is_open());
    EXPECT_EQ(file.FileSize(), 6);

    char buf[10];
    size_t bytes_read = file.Read(buf, 3);
    EXPECT_EQ(bytes_read, 3u);
    EXPECT_EQ(memcmp(buf, "foo", 3), 0);

    // FileSize() doesn't move the read position.
    EXPECT_EQ(file.FileSize(), 6);

    // Attempting to read past the end reads what is available
    // and sets the EOF flag.
    bytes_read = file.Read(buf, 5);
    EXPECT_EQ(bytes_read, 3u);
    EXPECT_EQ(memcmp(buf, "bar", 3), 0);
    EXPECT_TRUE(file.ReadEof());
  }

  // Clean up temporary file and directory.
  EXPECT_TRUE(test::RemoveFile(temp_filename));
  EXPECT_TRUE(test::RemoveDir(temp_dir));
}

}  // namespace webrtc
