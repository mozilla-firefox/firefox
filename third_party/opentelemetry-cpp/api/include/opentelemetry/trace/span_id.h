// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <cstring>

#include "opentelemetry/common/detail/ascii.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace trace
{

class SpanId final
{
public:
  // The size in bytes of the SpanId.
  static constexpr size_t kSize = 8;

  // An invalid SpanId (all zeros).
  SpanId() noexcept = default;

  // Creates a SpanId with the given ID.
  explicit SpanId(nostd::span<const uint8_t, kSize> id) noexcept
  {
    std::memcpy(rep_, id.data(), kSize);
  }

  // Populates the buffer with the lowercase base16 representation of the ID.
  void ToLowerBase16(nostd::span<char, 2 * kSize> buffer) const noexcept
  {
    for (size_t i = 0; i < kSize; ++i)
    {
      buffer[i * 2 + 0] = common::detail::LowerHexDigit(rep_[i] >> 4);
      buffer[i * 2 + 1] = common::detail::LowerHexDigit(rep_[i]);
    }
  }

  // Returns a nostd::span of the ID.
  nostd::span<const uint8_t, kSize> Id() const noexcept
  {
    return nostd::span<const uint8_t, kSize>(rep_);
  }

  bool operator==(const SpanId &that) const noexcept
  {
    return std::memcmp(rep_, that.rep_, kSize) == 0;
  }

  bool operator!=(const SpanId &that) const noexcept { return !(*this == that); }

  // Returns false if the SpanId is all zeros.
  bool IsValid() const noexcept
  {
    static constexpr uint8_t kEmptyRep[kSize] = {0};
    return std::memcmp(rep_, kEmptyRep, kSize) != 0;
  }

  // Copies the opaque SpanId data to dest.
  void CopyBytesTo(nostd::span<uint8_t, kSize> dest) const noexcept
  {
    std::memcpy(dest.data(), rep_, kSize);
  }

private:
  uint8_t rep_[kSize]{};
};

}  // namespace trace
OPENTELEMETRY_END_NAMESPACE
