// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace common
{
namespace detail
{

// Character classes for protocol and specification text, which define them over
// ASCII. The <cctype> functions depend on the global locale, and passing them a
// negative char is undefined behavior.

constexpr bool IsAsciiDigit(char c) noexcept
{
  return c >= '0' && c <= '9';
}

constexpr bool IsAsciiLower(char c) noexcept
{
  return c >= 'a' && c <= 'z';
}

constexpr bool IsAsciiUpper(char c) noexcept
{
  return c >= 'A' && c <= 'Z';
}

constexpr bool IsAsciiAlpha(char c) noexcept
{
  return IsAsciiLower(c) || IsAsciiUpper(c);
}

constexpr bool IsAsciiAlnum(char c) noexcept
{
  return IsAsciiAlpha(c) || IsAsciiDigit(c);
}

// Indexed by byte value: hexadecimal digits are decoded on the propagation hot
// path, where a lookup is faster than range comparisons.
constexpr int8_t kHexDigitValues[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

// Returns the value of a hexadecimal digit of either case, or -1 if c is not one.
constexpr int HexDigitValue(char c) noexcept
{
  return kHexDigitValues[static_cast<unsigned char>(c)];
}

constexpr bool IsAsciiHexDigit(char c) noexcept
{
  return HexDigitValue(c) >= 0;
}

// Returns the hexadecimal digit for the low four bits of value.
constexpr char LowerHexDigit(int value) noexcept
{
  return "0123456789abcdef"[value & 0xF];
}

constexpr char UpperHexDigit(int value) noexcept
{
  return "0123456789ABCDEF"[value & 0xF];
}

// 0x00 to 0x1F and 0x7F: what std::iscntrl accepts in the "C" locale.
constexpr bool IsAsciiControl(char c) noexcept
{
  return static_cast<unsigned char>(c) < 0x20 || c == '\x7f';
}

// Space, \t, \n, \v, \f and \r: what std::isspace accepts in the "C" locale.
constexpr bool IsAsciiSpace(char c) noexcept
{
  return c == ' ' || (c >= '\t' && c <= '\r');
}

constexpr char ToAsciiLower(char c) noexcept
{
  return IsAsciiUpper(c) ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr char ToAsciiUpper(char c) noexcept
{
  return IsAsciiLower(c) ? static_cast<char>(c - 'a' + 'A') : c;
}

}  // namespace detail
}  // namespace common
OPENTELEMETRY_END_NAMESPACE
