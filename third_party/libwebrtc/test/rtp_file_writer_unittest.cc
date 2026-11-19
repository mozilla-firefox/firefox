/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/rtp_file_writer.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "test/gmock.h"
#include "test/gtest.h"
#include "test/rtp_file_reader.h"
#include "test/testsupport/file_utils.h"

namespace webrtc {

using ::testing::Each;
using ::testing::Eq;

class RtpFileWriterTest : public ::testing::Test {
 public:
  void Init(const std::string& filename) {
    filename_ = test::TempFilename(test::OutputPath(), filename);
    rtp_writer_.reset(
        test::RtpFileWriter::Create(test::RtpFileWriter::kRtpDump, filename_));
  }

  void TearDown() override {
    rtp_writer_.reset();
    if (!filename_.empty()) {
      EXPECT_TRUE(test::RemoveFile(filename_));
    }
  }

  void WriteRtpPackets(int num_packets, int time_ms_offset = 0) {
    ASSERT_TRUE(rtp_writer_.get() != nullptr);
    test::RtpPacket packet;
    packet.buffer.EnsureCapacity(num_packets);
    for (int i = 1; i <= num_packets; ++i) {
      packet.buffer.SetSize(i);
      packet.original_length = i;
      packet.time_ms = i + time_ms_offset;
      std::ranges::fill(packet.buffer.AsMutableSpan(), i);
      EXPECT_TRUE(rtp_writer_->WritePacket(&packet));
    }
  }

  void CloseOutputFile() { rtp_writer_.reset(); }

  void VerifyFileContents(int expected_packets) {
    ASSERT_TRUE(rtp_writer_.get() == nullptr)
        << "Must call CloseOutputFile before VerifyFileContents";
    std::unique_ptr<test::RtpFileReader> rtp_reader(
        test::RtpFileReader::Create(test::RtpFileReader::kRtpDump, filename_));
    ASSERT_TRUE(rtp_reader.get() != nullptr);
    test::RtpPacket packet;
    int i = 0;
    while (rtp_reader->NextPacket(&packet)) {
      ++i;
      EXPECT_EQ(static_cast<size_t>(i), packet.length());
      EXPECT_EQ(static_cast<size_t>(i), packet.original_length);
      EXPECT_EQ(static_cast<uint32_t>(i - 1), packet.time_ms);
      EXPECT_THAT(packet.buffer, Each(Eq(i)));
    }
    EXPECT_EQ(expected_packets, i);
  }

 private:
  std::unique_ptr<test::RtpFileWriter> rtp_writer_;
  std::string filename_;
};

TEST_F(RtpFileWriterTest, WriteToRtpDump) {
  Init("test_rtp_file_writer.rtp");
  WriteRtpPackets(10);
  CloseOutputFile();
  VerifyFileContents(10);
}

TEST_F(RtpFileWriterTest, WriteToRtpDumpWithOffset) {
  Init("test_rtp_file_writer.rtp");
  WriteRtpPackets(10, 100);
  CloseOutputFile();
  VerifyFileContents(10);
}

}  // namespace webrtc
