/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/rtp_file_reader.h"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>

#include "modules/rtp_rtcp/source/rtp_util.h"
#include "test/gtest.h"
#include "test/testsupport/file_utils.h"

namespace webrtc {

class TestRtpFileReader : public ::testing::Test {
 public:
  void Init(const std::string& filename, bool headers_only_file) {
    std::string filepath =
        test::ResourcePath("video_coding/" + filename, "rtp");
    rtp_packet_source_.reset(
        test::RtpFileReader::Create(test::RtpFileReader::kRtpDump, filepath));
    ASSERT_TRUE(rtp_packet_source_.get() != nullptr);
    headers_only_file_ = headers_only_file;
  }

  int CountRtpPackets() {
    test::RtpPacket packet;
    int c = 0;
    while (rtp_packet_source_->NextPacket(&packet)) {
      if (headers_only_file_) {
        EXPECT_LT(packet.length(), packet.original_length);
      } else {
        EXPECT_EQ(packet.length(), packet.original_length);
      }
      c++;
    }
    return c;
  }

 private:
  std::unique_ptr<test::RtpFileReader> rtp_packet_source_;
  bool headers_only_file_;
};

TEST_F(TestRtpFileReader, Test60Packets) {
  Init("pltype103", false);
  EXPECT_EQ(60, CountRtpPackets());
}

TEST_F(TestRtpFileReader, Test60PacketsHeaderOnly) {
  Init("pltype103_header_only", true);
  EXPECT_EQ(60, CountRtpPackets());
}

typedef std::map<uint32_t, int> PacketsPerSsrc;

class TestPcapFileReader : public ::testing::Test {
 public:
  void Init(const std::string& filename) {
    std::string filepath =
        test::ResourcePath("video_coding/" + filename, "pcap");
    rtp_packet_source_.reset(
        test::RtpFileReader::Create(test::RtpFileReader::kPcap, filepath));
    ASSERT_TRUE(rtp_packet_source_.get() != nullptr);
  }

  int CountRtpPackets() {
    int c = 0;
    test::RtpPacket packet;
    while (rtp_packet_source_->NextPacket(&packet)) {
      EXPECT_EQ(packet.length(), packet.original_length);
      c++;
    }
    return c;
  }

  PacketsPerSsrc CountRtpPacketsPerSsrc() {
    PacketsPerSsrc pps;
    test::RtpPacket packet;
    while (rtp_packet_source_->NextPacket(&packet)) {
      if (IsRtpPacket(packet.buffer)) {
        pps[ParseRtpSsrc(packet.buffer)]++;
      }
    }
    return pps;
  }

 private:
  std::unique_ptr<test::RtpFileReader> rtp_packet_source_;
};

TEST_F(TestPcapFileReader, TestEthernetIIFrame) {
  Init("frame-ethernet-ii");
  EXPECT_EQ(368, CountRtpPackets());
}

TEST_F(TestPcapFileReader, TestLoopbackFrame) {
  Init("frame-loopback");
  EXPECT_EQ(491, CountRtpPackets());
}

TEST_F(TestPcapFileReader, TestTwoSsrc) {
  Init("ssrcs-2");
  PacketsPerSsrc pps = CountRtpPacketsPerSsrc();
  EXPECT_EQ(2UL, pps.size());
  EXPECT_EQ(370, pps[0x78d48f61]);
  EXPECT_EQ(60, pps[0xae94130b]);
}

TEST_F(TestPcapFileReader, TestThreeSsrc) {
  Init("ssrcs-3");
  PacketsPerSsrc pps = CountRtpPacketsPerSsrc();
  EXPECT_EQ(3UL, pps.size());
  EXPECT_EQ(162, pps[0x938c5eaa]);
  EXPECT_EQ(113, pps[0x59fe6ef0]);
  EXPECT_EQ(61, pps[0xed2bd2ac]);
}
}  // namespace webrtc
