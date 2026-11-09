// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "percent_decode.h"

#include <cstddef>

#include "opentelemetry/common/detail/ascii.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace resource
{
namespace detail
{
std::string PercentDecode(const std::string &value)
{
  std::string decoded;
  decoded.reserve(value.size());

  for (std::size_t i = 0; i < value.size(); ++i)
  {
    if (value[i] == '%' && i + 2 < value.size())
    {
      const int high = opentelemetry::common::detail::HexDigitValue(value[i + 1]);
      const int low  = opentelemetry::common::detail::HexDigitValue(value[i + 2]);

      if (high >= 0 && low >= 0)
      {
        decoded.push_back(static_cast<char>((high << 4) | low));
        i += 2;
        continue;
      }
    }

    decoded.push_back(value[i]);
  }

  return decoded;
}

}  // namespace detail
}  // namespace resource
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
