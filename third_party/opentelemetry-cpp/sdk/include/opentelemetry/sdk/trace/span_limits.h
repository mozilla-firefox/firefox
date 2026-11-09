// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace trace
{

/**
 * Span limits set on the TracerProvider and used by supporting recordables.
 * See https://opentelemetry.io/docs/specs/otel/trace/sdk/#span-limits.
 */
struct SpanLimits
{
  static constexpr std::uint32_t kDefaultAttributeCountLimit      = 128;
  static constexpr std::uint32_t kDefaultEventCountLimit          = 128;
  static constexpr std::uint32_t kDefaultLinkCountLimit           = 128;
  static constexpr std::uint32_t kDefaultEventAttributeCountLimit = 128;
  static constexpr std::uint32_t kDefaultLinkAttributeCountLimit  = 128;
  static constexpr std::size_t kDefaultAttributeValueLengthLimit =
      (std::numeric_limits<std::size_t>::max)();

  /// Maximum number of attributes per span.
  std::uint32_t attribute_count_limit{kDefaultAttributeCountLimit};

  /// Maximum allowed attribute value length (applies to string values and byte arrays)
  std::size_t attribute_value_length_limit{kDefaultAttributeValueLengthLimit};

  /// Maximum number of events per span.
  std::uint32_t event_count_limit{kDefaultEventCountLimit};

  /// Maximum number of links per span.
  std::uint32_t link_count_limit{kDefaultLinkCountLimit};

  /// Maximum number of attributes per span event.
  std::uint32_t event_attribute_count_limit{kDefaultEventAttributeCountLimit};

  /// Maximum number of attributes per span link.
  std::uint32_t link_attribute_count_limit{kDefaultLinkAttributeCountLimit};

  /**
   * Limit values that impose no cap on any field
   */
  static constexpr SpanLimits NoLimits() noexcept
  {
    SpanLimits limits;
    limits.attribute_count_limit        = (std::numeric_limits<std::uint32_t>::max)();
    limits.attribute_value_length_limit = (std::numeric_limits<std::size_t>::max)();
    limits.event_count_limit            = (std::numeric_limits<std::uint32_t>::max)();
    limits.link_count_limit             = (std::numeric_limits<std::uint32_t>::max)();
    limits.event_attribute_count_limit  = (std::numeric_limits<std::uint32_t>::max)();
    limits.link_attribute_count_limit   = (std::numeric_limits<std::uint32_t>::max)();
    return limits;
  }
};

namespace span_limits_env
{

/**
 * @brief Returns span limits read from the environment variables defined in the
 * OpenTelemetry specification
 * (https://opentelemetry.io/docs/specs/otel/configuration/sdk-environment-variables/#span-limits).
 *
 * A field whose environment variable is not set keeps its value from @p defaults, so with no
 * variables set the result equals @p defaults. The default of SpanLimits::NoLimits() leaves SDK
 * behavior unchanged; pass SpanLimits{} to fall back to the specification defaults instead. The
 * span-specific variables (OTEL_SPAN_ATTRIBUTE_VALUE_LENGTH_LIMIT,
 * OTEL_SPAN_ATTRIBUTE_COUNT_LIMIT) take precedence over the general ones
 * (OTEL_ATTRIBUTE_VALUE_LENGTH_LIMIT, OTEL_ATTRIBUTE_COUNT_LIMIT).
 */
OPENTELEMETRY_EXPORT SpanLimits
GetSpanLimitsFromEnv(const SpanLimits &defaults = SpanLimits::NoLimits());

}  // namespace span_limits_env

}  // namespace trace
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
