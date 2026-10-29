/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_RTP_RTCP_SOURCE_RTP_FORMAT_H_
#define MODULES_RTP_RTCP_SOURCE_RTP_FORMAT_H_

#include <stdint.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "absl/base/nullability.h"
#include "modules/rtp_rtcp/source/rtp_video_header.h"

namespace webrtc {

class RtpPacketToSend;

class RtpPacketizer {
 public:
  struct PayloadSizeLimits {
    int max_payload_len = 1200;
    int first_packet_reduction_len = 0;
    int last_packet_reduction_len = 0;
    // Reduction len for packet that is first & last at the same time.
    int single_packet_reduction_len = 0;

    PayloadSizeLimits Sanitize() const {
      PayloadSizeLimits result = *this;
      result.max_payload_len = std::max(0, result.max_payload_len);
      result.single_packet_reduction_len = std::clamp(
          result.single_packet_reduction_len, 0, result.max_payload_len);
      result.first_packet_reduction_len = std::clamp(
          result.first_packet_reduction_len, 0, result.max_payload_len);
      result.last_packet_reduction_len = std::clamp(
          result.last_packet_reduction_len, 0, result.max_payload_len);
      return result;
    }
  };

  enum class PacketizationFormat {
    kGeneric,
    kRaw,
    kH264,
    kH265,
    kVP8,
    kVP9,
    kAV1,
    kSframe,
  };
  static absl_nonnull std::unique_ptr<RtpPacketizer> Create(
      PacketizationFormat format,
      std::span<const uint8_t> payload,
      PayloadSizeLimits limits,
      // Codec-specific details.
      const RTPVideoHeader& rtp_video_header);

  virtual ~RtpPacketizer() = default;

  // Returns number of remaining packets to produce by the packetizer.
  virtual size_t NumPackets() const = 0;

  // Get the next payload with payload header.
  // Write payload and set marker bit of the `packet`.
  // Returns true on success, false otherwise.
  virtual bool NextPacket(RtpPacketToSend* packet) = 0;

  // Splits `payload_size` into sum of integers with respect to `limits`.
  // Returns empty vector on failure.
  static std::vector<int> SplitAboutEqually(size_t payload_size,
                                            const PayloadSizeLimits& limits);
};
}  // namespace webrtc
#endif  // MODULES_RTP_RTCP_SOURCE_RTP_FORMAT_H_
