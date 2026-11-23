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

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include "rtc_base/checks.h"
#include "test/rtp_file_reader.h"

namespace webrtc {
namespace test {

static const uint16_t kPacketHeaderSize = 8;
static const char kFirstLine[] = "#!rtpplay1.0 0.0.0.0/0\n";

// Write RTP packets to file in rtpdump format, as documented at:
// http://www.cs.columbia.edu/irt/software/rtptools/
class RtpDumpWriter : public RtpFileWriter {
 public:
  explicit RtpDumpWriter(FILE* file) : file_(file) {
    RTC_CHECK(file_ != nullptr);
    Init();
  }
  ~RtpDumpWriter() override {
    if (file_ != nullptr) {
      fclose(file_);
      file_ = nullptr;
    }
  }

  RtpDumpWriter(const RtpDumpWriter&) = delete;
  RtpDumpWriter& operator=(const RtpDumpWriter&) = delete;

  bool WritePacket(const RtpPacket* packet) override {
    if (!first_packet_time_) {
      first_packet_time_ = packet->time_ms;
    }
    uint16_t len = static_cast<uint16_t>(packet->length() + kPacketHeaderSize);
    uint16_t plen = static_cast<uint16_t>(packet->original_length);
    uint32_t offset = packet->time_ms - *first_packet_time_;
    RTC_CHECK(WriteUint16(len));
    RTC_CHECK(WriteUint16(plen));
    RTC_CHECK(WriteUint32(offset));
    return fwrite(packet->buffer.data(), sizeof(uint8_t), packet->length(),
                  file_) == packet->length();
  }

 private:
  bool Init() {
    fprintf(file_, "%s", kFirstLine);

    RTC_CHECK(WriteUint32(0));
    RTC_CHECK(WriteUint32(0));
    RTC_CHECK(WriteUint32(0));
    RTC_CHECK(WriteUint16(0));
    RTC_CHECK(WriteUint16(0));

    return true;
  }

  bool WriteUint32(uint32_t in) {
    // Loop through shifts = {24, 16, 8, 0}.
    for (int shifts = 24; shifts >= 0; shifts -= 8) {
      uint8_t tmp = static_cast<uint8_t>((in >> shifts) & 0xFF);
      if (fwrite(&tmp, sizeof(uint8_t), 1, file_) != 1)
        return false;
    }
    return true;
  }

  bool WriteUint16(uint16_t in) {
    // Write 8 MSBs.
    uint8_t tmp = static_cast<uint8_t>((in >> 8) & 0xFF);
    if (fwrite(&tmp, sizeof(uint8_t), 1, file_) != 1)
      return false;
    // Write 8 LSBs.
    tmp = static_cast<uint8_t>(in & 0xFF);
    if (fwrite(&tmp, sizeof(uint8_t), 1, file_) != 1)
      return false;
    return true;
  }

  FILE* file_;
  std::optional<uint32_t> first_packet_time_;
};

RtpFileWriter* RtpFileWriter::Create(FileFormat format,
                                     const std::string& filename) {
  FILE* file = fopen(filename.c_str(), "wb");
  if (file == nullptr) {
    printf("ERROR: Can't open file: %s\n", filename.c_str());
    return nullptr;
  }
  switch (format) {
    case kRtpDump:
      return new RtpDumpWriter(file);
  }
  fclose(file);
  return nullptr;
}

}  // namespace test
}  // namespace webrtc
