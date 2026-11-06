// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <string>

#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace common
{

/**
 * Matches a text string against a pattern containing '?' (any single character)
 * and '*' (any sequence of characters, including empty) wildcards.
 */
inline bool WildcardMatch(const std::string &pattern, const std::string &text)
{
  size_t p      = 0;
  size_t t      = 0;
  size_t star_p = std::string::npos;
  size_t star_t = 0;

  while (t < text.size())
  {
    if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t]))
    {
      ++p;
      ++t;
    }
    else if (p < pattern.size() && pattern[p] == '*')
    {
      star_p = p;
      star_t = t;
      ++p;
    }
    else if (star_p != std::string::npos)
    {
      p = star_p + 1;
      ++star_t;
      t = star_t;
    }
    else
    {
      return false;
    }
  }

  while (p < pattern.size() && pattern[p] == '*')
  {
    ++p;
  }

  return p == pattern.size();
}

}  // namespace common
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
