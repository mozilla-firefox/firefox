// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "opentelemetry/common/detail/ascii.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace trace
{

// TraceFlags represents options for a Trace. These options are propagated to all child Spans
// and determine features such as whether a Span should be traced. TraceFlags
// are implemented as a bitmask.
class TraceFlags final
{
public:
  static constexpr uint8_t kIsSampled = 1;
  static constexpr uint8_t kIsRandom  = 2;

  /**
   * Valid flags in W3C Trace Context version 1.
   * See https://www.w3.org/TR/trace-context-1/#trace-flags
   */
  static constexpr uint8_t kAllW3CTraceContext1Flags = kIsSampled;

  /**
   * Valid flags in W3C Trace Context version 2.
   * See https://www.w3.org/TR/trace-context-2/#trace-flags
   */
  static constexpr uint8_t kAllW3CTraceContext2Flags = kIsSampled | kIsRandom;

  TraceFlags() noexcept : rep_{0} {}

  explicit TraceFlags(uint8_t flags) noexcept : rep_(flags) {}

  bool IsSampled() const noexcept { return rep_ & kIsSampled; }

  bool IsRandom() const noexcept { return rep_ & kIsRandom; }

  // Populates the buffer with the lowercase base16 representation of the flags.
  void ToLowerBase16(nostd::span<char, 2> buffer) const noexcept
  {
    buffer[0] = common::detail::UpperHexDigit(rep_ >> 4);
    buffer[1] = common::detail::UpperHexDigit(rep_);
  }

  uint8_t flags() const noexcept { return rep_; }

  bool operator==(const TraceFlags &that) const noexcept { return rep_ == that.rep_; }

  bool operator!=(const TraceFlags &that) const noexcept { return !(*this == that); }

  // Copies the TraceFlags to dest.
  void CopyBytesTo(nostd::span<uint8_t, 1> dest) const noexcept { dest[0] = rep_; }

private:
  uint8_t rep_;
};

}  // namespace trace
OPENTELEMETRY_END_NAMESPACE
