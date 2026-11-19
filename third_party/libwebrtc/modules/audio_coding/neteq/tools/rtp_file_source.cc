/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_coding/neteq/tools/rtp_file_source.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>

#include "absl/strings/string_view.h"
#include "api/rtp_header_extension_id.h"
#include "api/units/timestamp.h"
#include "modules/audio_coding/neteq/tools/packet_source.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "rtc_base/checks.h"
#include "test/rtp_file_reader.h"

namespace webrtc {
namespace test {

RtpFileSource* RtpFileSource::Create(absl::string_view file_name,
                                     std::optional<uint32_t> ssrc_filter) {
  RtpFileSource* source = new RtpFileSource(ssrc_filter);
  RTC_CHECK(source->OpenFile(file_name));
  return source;
}

bool RtpFileSource::ValidRtpDump(absl::string_view file_name) {
  std::unique_ptr<RtpFileReader> temp_file(
      RtpFileReader::Create(RtpFileReader::kRtpDump, file_name));
  return !!temp_file;
}

bool RtpFileSource::ValidPcap(absl::string_view file_name) {
  std::unique_ptr<RtpFileReader> temp_file(
      RtpFileReader::Create(RtpFileReader::kPcap, file_name));
  return !!temp_file;
}

RtpFileSource::~RtpFileSource() {}

bool RtpFileSource::RegisterRtpHeaderExtension(RTPExtensionType type,
                                               RtpHeaderExtensionId id) {
  return rtp_header_extension_map_.RegisterByType(id, type);
}

std::unique_ptr<RtpPacketReceived> RtpFileSource::NextPacket() {
  while (true) {
    RtpPacket temp_packet;
    if (!rtp_reader_->NextPacket(&temp_packet)) {
      return nullptr;
    }
    if (temp_packet.original_length == 0) {
      // May be an RTCP packet.
      // Read the next one.
      continue;
    }
    auto rtp_packet =
        std::make_unique<RtpPacketReceived>(&rtp_header_extension_map_);
    if (!rtp_packet->Parse(temp_packet.buffer)) {
      continue;
    }
    if (filter_.test(rtp_packet->PayloadType()) ||
        (ssrc_filter_ && rtp_packet->Ssrc() != *ssrc_filter_)) {
      // This payload type should be filtered out. Continue to the next packet.
      continue;
    }
    rtp_packet->set_arrival_time(Timestamp::Millis(temp_packet.time_ms));

    // Simulate payload if only the RTP header was written in the file.
    if (temp_packet.original_length > rtp_packet->size()) {
      size_t payload_size =
          temp_packet.original_length - rtp_packet->headers_size();
      if (rtp_packet->has_padding()) {
        // If padding bit is set in the RTP header, assume it was a pure padding
        // packet.
        rtp_packet->SetPadding(payload_size);
      } else {
        std::fill_n(rtp_packet->AllocatePayload(payload_size), payload_size, 0);
      }
    }
    return rtp_packet;
  }
}

RtpFileSource::RtpFileSource(std::optional<uint32_t> ssrc_filter)
    : PacketSource(), ssrc_filter_(ssrc_filter) {}

bool RtpFileSource::OpenFile(absl::string_view file_name) {
  rtp_reader_.reset(RtpFileReader::Create(RtpFileReader::kRtpDump, file_name));
  if (rtp_reader_)
    return true;
  rtp_reader_.reset(RtpFileReader::Create(RtpFileReader::kPcap, file_name));
  if (!rtp_reader_) {
    RTC_FATAL()
        << "Couldn't open input file as either a rtpdump or .pcap. Note "
        << "that .pcapng is not supported.";
  }
  return true;
}

}  // namespace test
}  // namespace webrtc
