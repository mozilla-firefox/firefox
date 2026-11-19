/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "pc/congestion_control_feedback_block.h"
#include "pc/congestion_control_feedback_report.h"
#include "rtc_base/checks.h"
#include "test/fuzzers/fuzz_data_helper.h"

namespace webrtc {

void FuzzOneInput(FuzzDataHelper fuzz_data) {
  // Encode() and Decode() allow at most one tick per microsecond.
  const int fuzzed_ticks_per_second = static_cast<int>(
      1 + fuzz_data.ReadOrDefaultValue<uint32_t>(0) % 1'000'000);
  const std::span<const uint8_t> input = fuzz_data.ReadRemaining();

  for (int ato_ticks_per_second :
       {CongestionControlFeedbackBlock::kDefaultAtoTicksPerSecond,
        fuzzed_ticks_per_second}) {
    std::optional<CongestionControlFeedbackReport> report =
        CongestionControlFeedbackReport::Decode(input, ato_ticks_per_second);
    if (!report.has_value()) {
      return;
    }
    RTC_CHECK_LE(report->SizeBytes(), input.size());

    // Compare reports, not bytes: the input may hold bits Decode() ignores.
    std::vector<uint8_t> bytes(report->SizeBytes());
    report->Encode(bytes, ato_ticks_per_second);
    RTC_CHECK(CongestionControlFeedbackReport::Decode(
                  bytes, ato_ticks_per_second) == report);
  }
}

}  // namespace webrtc
