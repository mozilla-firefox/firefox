// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "opentelemetry/common/detail/ascii.h"
#include "opentelemetry/common/macros.h"

#if OPENTELEMETRY_HAVE_EXCEPTIONS
#  include <stdexcept>
#endif

#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace common
{

class StringUtil
{
public:
  static nostd::string_view Trim(nostd::string_view str OPENTELEMETRY_ATTRIBUTE_LIFETIME_BOUND,
                                 size_t left,
                                 size_t right) noexcept
  {
    if (right >= str.size())
    {
      return nostd::string_view();
    }
    while (left <= right && detail::IsAsciiSpace(str[left]))
    {
      left++;
    }
    while (left <= right && detail::IsAsciiSpace(str[right]))
    {
      right--;
    }
    if (left > right)
    {
      return nostd::string_view();
    }
#if OPENTELEMETRY_HAVE_EXCEPTIONS
    try
#endif
    {
      return str.substr(left, 1 + right - left);
    }
#if OPENTELEMETRY_HAVE_EXCEPTIONS
    catch (const std::out_of_range &)
    {
      return nostd::string_view();
    }
#endif
  }

  static nostd::string_view Substr(nostd::string_view str OPENTELEMETRY_ATTRIBUTE_LIFETIME_BOUND,
                                   size_t pos,
                                   size_t count = nostd::string_view::npos) noexcept
  {
#if OPENTELEMETRY_HAVE_EXCEPTIONS
    try
#endif
    {
      return str.substr(pos, count);
    }
#if OPENTELEMETRY_HAVE_EXCEPTIONS
    catch (const std::out_of_range &)
    {
      return nostd::string_view();
    }
#endif
  }

  static nostd::string_view Trim(
      nostd::string_view str OPENTELEMETRY_ATTRIBUTE_LIFETIME_BOUND) noexcept
  {
    if (str.empty())
    {
      return str;
    }

    return Trim(str, 0, str.size() - 1);
  }
};

}  // namespace common

OPENTELEMETRY_END_NAMESPACE
