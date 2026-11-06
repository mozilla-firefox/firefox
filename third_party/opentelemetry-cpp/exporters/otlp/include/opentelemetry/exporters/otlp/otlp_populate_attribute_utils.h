// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <limits>
#include <string>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/macros.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/version.h"

namespace opentelemetry
{
namespace proto
{

namespace common
{
namespace v1
{
class AnyValue;
class KeyValue;
class InstrumentationScope;
}  // namespace v1
}  // namespace common

namespace resource
{
namespace v1
{
class Resource;
}
}  // namespace resource

}  // namespace proto
}  // namespace opentelemetry

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

/**
 * AttributeConverterOptions control the behavior of OtlpPopulateAttributeUtils::PopulateAnyValue
 * and OtlpPopulateAttributeUtils::PopulateAttribute.
 *
 * When `attribute_value_length_limit` is less than `std::numeric_limits<std::size_t>::max()`,
 * string attribute values are truncated to at most `attribute_value_length_limit` bytes using
 * UTF-8-safe truncation (Utf8SafePrefixLength) so the resulting protobuf
 * `string_value` stays valid UTF-8 when the input was. Byte array attribute values
 * (span<const uint8_t>) are truncated at the raw byte boundary. Non-string
 * alternatives are unaffected.
 *
 */
struct AttributeConverterOptions
{
  /// Maximum length of string or bytes attribute values in bytes. When the value is longer than
  /// this limit, it will be truncated to this limit. The default value is no limit.
  std::size_t attribute_value_length_limit{(std::numeric_limits<std::size_t>::max)()};

  AttributeConverterOptions() noexcept = default;
  explicit AttributeConverterOptions(std::size_t value_length_limit) noexcept
      : attribute_value_length_limit(value_length_limit)
  {}
};

/**
 * The OtlpPopulateAttributeUtils contains utility functions to populate attributes
 */
class OtlpPopulateAttributeUtils
{

public:
  static void PopulateAttribute(opentelemetry::proto::resource::v1::Resource *proto,
                                const opentelemetry::sdk::resource::Resource &resource) noexcept;

  static void PopulateAttribute(opentelemetry::proto::common::v1::InstrumentationScope *proto,
                                const opentelemetry::sdk::instrumentationscope::InstrumentationScope
                                    &instrumentation_scope) noexcept;

  static bool PopulateAnyValue(
      opentelemetry::proto::common::v1::AnyValue *proto_value,
      const opentelemetry::common::AttributeValue &value,
      AttributeConverterOptions options = AttributeConverterOptions{}) noexcept;

  static bool PopulateAnyValue(
      opentelemetry::proto::common::v1::AnyValue *proto_value,
      const opentelemetry::sdk::common::OwnedAttributeValue &value,
      AttributeConverterOptions options = AttributeConverterOptions{}) noexcept;

  static void PopulateAttribute(
      opentelemetry::proto::common::v1::KeyValue *attribute,
      nostd::string_view key,
      const opentelemetry::common::AttributeValue &value,
      AttributeConverterOptions options = AttributeConverterOptions{}) noexcept;

  static void PopulateAttribute(
      opentelemetry::proto::common::v1::KeyValue *attribute,
      nostd::string_view key,
      const opentelemetry::sdk::common::OwnedAttributeValue &value,
      AttributeConverterOptions options = AttributeConverterOptions{}) noexcept;

  /**
   * Byte length of the longest prefix of `value` that fits within `max_bytes`
   * without splitting a well-formed UTF-8 multi-byte sequence. A lead byte's
   * declared length is only honored when its continuation bytes are present
   * and in range (0x80-0xBF); otherwise the lead is treated as a one-byte
   * unit, so malformed input degrades to plain byte truncation. The protobuf
   * `string` field type requires valid UTF-8, so this utility lets callers
   * truncate at a code-point boundary instead of cutting through a multi-byte
   * sequence.
   */
  static std::size_t Utf8SafePrefixLength(const char *data,
                                          std::size_t size,
                                          std::size_t max_bytes) noexcept;

  /// Convenience overload that delegates to the pointer + size variant.
  static std::size_t Utf8SafePrefixLength(const std::string &value, std::size_t max_bytes) noexcept
  {
    return Utf8SafePrefixLength(value.data(), value.size(), max_bytes);
  }
};

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
