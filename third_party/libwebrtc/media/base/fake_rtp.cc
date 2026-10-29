/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/base/fake_rtp.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "absl/algorithm/container.h"
#include "api/rtp_header_extension_id.h"
#include "rtc_base/checks.h"
#include "test/gtest.h"

namespace webrtc {

void CompareHeaderExtensions(
    std::span<const uint8_t> packet1,
    std::span<const uint8_t> packet2,
    std::span<const RtpHeaderExtensionId> encrypted_headers,
    bool expect_equal) {
  // Sanity check: packets must be large enough to contain the RTP header and
  // extensions header.
  RTC_CHECK_GE(packet1.size(), 12 + 4);
  RTC_CHECK_GE(packet2.size(), 12 + 4);
  // RTP extension headers are the same.
  EXPECT_TRUE(
      std::ranges::equal(packet1.subspan<12, 4>(), packet2.subspan<12, 4>()));
  // Check for one-byte header extensions.
  EXPECT_EQ(packet1[12], 0xBE);
  EXPECT_EQ(packet1[13], 0xDE);
  // Determine position and size of extension headers.
  size_t extension_words = packet1[14] << 8 | packet1[15];
  std::span<const uint8_t> extension_data1 =
      packet1.subspan(12 + 4, extension_words * 4);
  std::span<const uint8_t> extension_data2 = packet2.subspan(12 + 4);
  // Sanity check: packets must be large enough to contain the RTP header
  // extensions.
  RTC_CHECK_GE(packet1.size(), 12 + 4 + extension_words * 4);
  RTC_CHECK_GE(packet2.size(), 12 + 4 + extension_words * 4);
  while (!extension_data1.empty()) {
    uint8_t id = (extension_data1[0] & 0xf0) >> 4;
    uint8_t len = (extension_data1[0] & 0x0f) + 1;
    extension_data1 = extension_data1.subspan(1);
    extension_data2 = extension_data2.subspan(1);
    if (id == 15) {
      // Finished parsing.
      break;
    }

    // The header extension doesn't get encrypted if the id is not in the
    // list of header extensions to encrypt.
    if (expect_equal ||
        !absl::c_linear_search(encrypted_headers, RtpHeaderExtensionId(id))) {
      EXPECT_TRUE(std::ranges::equal(extension_data1.first(len),
                                     extension_data2.first(len)));
    } else {
      EXPECT_FALSE(std::ranges::equal(extension_data1.first(len),
                                      extension_data2.first(len)));
    }

    extension_data1 = extension_data1.subspan(len);
    extension_data2 = extension_data2.subspan(len);
    // Skip padding.
    while (!extension_data1.empty() && extension_data1[0] == 0) {
      extension_data1 = extension_data1.subspan(1);
      extension_data2 = extension_data2.subspan(1);
    }
  }
}

}  // namespace webrtc
