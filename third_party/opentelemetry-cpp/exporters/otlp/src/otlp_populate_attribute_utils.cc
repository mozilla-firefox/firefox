// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#if defined(ENABLE_OTLP_UTF8_VALIDITY)
#  include <utf8_validity.h>
#endif

#include <stdint.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/exporters/otlp/otlp_populate_attribute_utils.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/version.h"

// clang-format off
#include "opentelemetry/exporters/otlp/protobuf_include_prefix.h" // IWYU pragma: keep
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"
#include "opentelemetry/exporters/otlp/protobuf_include_suffix.h" // IWYU pragma: keep
// clang-format on

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

//
// See `attribute_value.h` for details.
//
const int kAttributeValueSize      = 16;
const int kOwnedAttributeValueSize = 15;

namespace
{
// Per OpenTelemetry spec, uint64_t attribute values exceeding INT64_MAX must be
// encoded as a decimal string rather than wrapping to a negative int64 via narrowing.
// https://opentelemetry.io/docs/specs/otel/common/attribute-type-mapping/#integer-values
inline void SetUint64Value(opentelemetry::proto::common::v1::AnyValue *proto_value, uint64_t val)
{
  if (val <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
  {
    proto_value->set_int_value(static_cast<int64_t>(val));
  }
  else
  {
    proto_value->set_string_value(std::to_string(val));
  }
}
}  // namespace

void OtlpPopulateAttributeUtils::PopulateAnyValue(
    opentelemetry::proto::common::v1::AnyValue *proto_value,
    const opentelemetry::common::AttributeValue &value,
    bool allow_bytes,
    std::size_t max_length) noexcept
{
  if (nullptr == proto_value)
  {
    return;
  }

  // Assert size of variant to ensure that this method gets updated if the variant
  // definition changes
  static_assert(
      nostd::variant_size<opentelemetry::common::AttributeValue>::value == kAttributeValueSize,
      "AttributeValue contains unknown type");

  if (nostd::holds_alternative<bool>(value))
  {
    proto_value->set_bool_value(nostd::get<bool>(value));
  }
  else if (nostd::holds_alternative<int>(value))
  {
    proto_value->set_int_value(nostd::get<int>(value));
  }
  else if (nostd::holds_alternative<int64_t>(value))
  {
    proto_value->set_int_value(nostd::get<int64_t>(value));
  }
  else if (nostd::holds_alternative<unsigned int>(value))
  {
    proto_value->set_int_value(nostd::get<unsigned int>(value));
  }
  else if (nostd::holds_alternative<uint64_t>(value))
  {
    SetUint64Value(proto_value, nostd::get<uint64_t>(value));
  }
  else if (nostd::holds_alternative<double>(value))
  {
    proto_value->set_double_value(nostd::get<double>(value));
  }
  else if (nostd::holds_alternative<const char *>(value))
  {
    const char *str_value      = nostd::get<const char *>(value);
    const std::size_t str_len  = std::strlen(str_value);
    const std::size_t kept_len = Utf8SafePrefixLength(str_value, str_len, max_length);
#if defined(ENABLE_OTLP_UTF8_VALIDITY)
    // Validity is decided by the original input, so truncation that happens
    // to remove invalid bytes does not flip the proto field type.
    if (utf8_range::IsStructurallyValid({str_value, str_len}))
    {
      proto_value->set_string_value(str_value, kept_len);
    }
    else
    {
      proto_value->set_bytes_value(str_value, kept_len);
    }
#else
    proto_value->set_string_value(str_value, kept_len);
#endif
  }
  else if (nostd::holds_alternative<nostd::string_view>(value))
  {
    nostd::string_view str_value = nostd::get<nostd::string_view>(value);
    const std::size_t kept_len =
        Utf8SafePrefixLength(str_value.data(), str_value.size(), max_length);
#if defined(ENABLE_OTLP_UTF8_VALIDITY)
    if (utf8_range::IsStructurallyValid({str_value.data(), str_value.size()}))
    {
      proto_value->set_string_value(str_value.data(), kept_len);
    }
    else
    {
      proto_value->set_bytes_value(str_value.data(), kept_len);
    }
#else
    proto_value->set_string_value(str_value.data(), kept_len);
#endif
  }
  else if (nostd::holds_alternative<nostd::span<const uint8_t>>(value))
  {
    const auto &bytes = nostd::get<nostd::span<const uint8_t>>(value);
    if (allow_bytes)
    {
      const std::size_t kept_len = (std::min)(bytes.size(), max_length);
      proto_value->set_bytes_value(reinterpret_cast<const void *>(bytes.data()), kept_len);
    }
    else
    {
      auto array_value = proto_value->mutable_array_value();
      for (const auto &val : bytes)
      {
        array_value->add_values()->set_int_value(val);
      }
    }
  }
  else if (nostd::holds_alternative<nostd::span<const bool>>(value))
  {
    auto array_value = proto_value->mutable_array_value();
    for (const auto &val : nostd::get<nostd::span<const bool>>(value))
    {
      array_value->add_values()->set_bool_value(val);
    }
  }
  else if (nostd::holds_alternative<nostd::span<const int>>(value))
  {
    auto array_value = proto_value->mutable_array_value();
    for (const auto &val : nostd::get<nostd::span<const int>>(value))
    {
      array_value->add_values()->set_int_value(val);
    }
  }
  else if (nostd::holds_alternative<nostd::span<const int64_t>>(value))
  {
    auto array_value = proto_value->mutable_array_value();
    for (const auto &val : nostd::get<nostd::span<const int64_t>>(value))
    {
      array_value->add_values()->set_int_value(val);
    }
  }
  else if (nostd::holds_alternative<nostd::span<const unsigned int>>(value))
  {
    auto array_value = proto_value->mutable_array_value();
    for (const auto &val : nostd::get<nostd::span<const unsigned int>>(value))
    {
      array_value->add_values()->set_int_value(val);
    }
  }
  else if (nostd::holds_alternative<nostd::span<const uint64_t>>(value))
  {
    auto array_value = proto_value->mutable_array_value();
    for (const auto &val : nostd::get<nostd::span<const uint64_t>>(value))
    {
      SetUint64Value(array_value->add_values(), val);
    }
  }
  else if (nostd::holds_alternative<nostd::span<const double>>(value))
  {
    auto array_value = proto_value->mutable_array_value();
    for (const auto &val : nostd::get<nostd::span<const double>>(value))
    {
      array_value->add_values()->set_double_value(val);
    }
  }
  else if (nostd::holds_alternative<nostd::span<const nostd::string_view>>(value))
  {
    auto array_value = proto_value->mutable_array_value();
    for (const auto &val : nostd::get<nostd::span<const nostd::string_view>>(value))
    {
      const std::size_t kept_len = Utf8SafePrefixLength(val.data(), val.size(), max_length);
#if defined(ENABLE_OTLP_UTF8_VALIDITY)
      if (utf8_range::IsStructurallyValid({val.data(), val.size()}))
      {
        array_value->add_values()->set_string_value(val.data(), kept_len);
      }
      else
      {
        array_value->add_values()->set_bytes_value(val.data(), kept_len);
      }
#else
      array_value->add_values()->set_string_value(val.data(), kept_len);
#endif
    }
  }
}

void OtlpPopulateAttributeUtils::PopulateAnyValue(
    opentelemetry::proto::common::v1::AnyValue *proto_value,
    const opentelemetry::sdk::common::OwnedAttributeValue &value,
    bool allow_bytes,
    std::size_t max_length) noexcept
{
  if (nullptr == proto_value)
  {
    return;
  }

  // Assert size of variant to ensure that this method gets updated if the variant
  // definition changes
  static_assert(nostd::variant_size<opentelemetry::sdk::common::OwnedAttributeValue>::value ==
                    kOwnedAttributeValueSize,
                "OwnedAttributeValue contains unknown type");

  if (nostd::holds_alternative<bool>(value))
  {
    proto_value->set_bool_value(nostd::get<bool>(value));
  }
  else if (nostd::holds_alternative<int32_t>(value))
  {
    proto_value->set_int_value(nostd::get<int32_t>(value));
  }
  else if (nostd::holds_alternative<int64_t>(value))
  {
    proto_value->set_int_value(nostd::get<int64_t>(value));
  }
  else if (nostd::holds_alternative<uint32_t>(value))
  {
    proto_value->set_int_value(nostd::get<uint32_t>(value));
  }
  else if (nostd::holds_alternative<uint64_t>(value))
  {
    SetUint64Value(proto_value, nostd::get<uint64_t>(value));
  }
  else if (nostd::holds_alternative<double>(value))
  {
    proto_value->set_double_value(nostd::get<double>(value));
  }
  else if (nostd::holds_alternative<std::vector<uint8_t>>(value))
  {
    if (allow_bytes)
    {
      const std::vector<uint8_t> &byte_array = nostd::get<std::vector<uint8_t>>(value);
      const std::size_t kept_len             = (std::min)(byte_array.size(), max_length);
      proto_value->set_bytes_value(reinterpret_cast<const void *>(byte_array.data()), kept_len);
    }
    else
    {
      auto array_value = proto_value->mutable_array_value();
      for (const auto &val : nostd::get<std::vector<uint8_t>>(value))
      {
        array_value->add_values()->set_int_value(val);
      }
    }
  }
  else if (nostd::holds_alternative<std::string>(value))
  {
    const std::string &str_value = nostd::get<std::string>(value);
    const std::size_t kept_len   = Utf8SafePrefixLength(str_value, max_length);
#if defined(ENABLE_OTLP_UTF8_VALIDITY)
    if (utf8_range::IsStructurallyValid(str_value))
    {
      proto_value->set_string_value(str_value.data(), kept_len);
    }
    else
    {
      proto_value->set_bytes_value(str_value.data(), kept_len);
    }
#else
    proto_value->set_string_value(str_value.data(), kept_len);
#endif
  }
  else if (nostd::holds_alternative<std::vector<bool>>(value))
  {
    auto array_value = proto_value->mutable_array_value();
    for (const auto val : nostd::get<std::vector<bool>>(value))
    {
      array_value->add_values()->set_bool_value(val);
    }
  }
  else if (nostd::holds_alternative<std::vector<int32_t>>(value))
  {
    auto array_value = proto_value->mutable_array_value();
    for (const auto &val : nostd::get<std::vector<int32_t>>(value))
    {
      array_value->add_values()->set_int_value(val);
    }
  }
  else if (nostd::holds_alternative<std::vector<uint32_t>>(value))
  {
    auto array_value = proto_value->mutable_array_value();
    for (const auto &val : nostd::get<std::vector<uint32_t>>(value))
    {
      array_value->add_values()->set_int_value(
          val);  // NOLINT(cppcoreguidelines-narrowing-conversions)
    }
  }
  else if (nostd::holds_alternative<std::vector<int64_t>>(value))
  {
    auto array_value = proto_value->mutable_array_value();
    for (const auto &val : nostd::get<std::vector<int64_t>>(value))
    {
      array_value->add_values()->set_int_value(val);
    }
  }
  else if (nostd::holds_alternative<std::vector<uint64_t>>(value))
  {
    auto array_value = proto_value->mutable_array_value();
    for (const auto &val : nostd::get<std::vector<uint64_t>>(value))
    {
      SetUint64Value(array_value->add_values(), val);
    }
  }
  else if (nostd::holds_alternative<std::vector<double>>(value))
  {
    auto array_value = proto_value->mutable_array_value();
    for (const auto &val : nostd::get<std::vector<double>>(value))
    {
      array_value->add_values()->set_double_value(val);
    }
  }
  else if (nostd::holds_alternative<std::vector<std::string>>(value))
  {
    auto array_value = proto_value->mutable_array_value();
    for (const auto &val : nostd::get<std::vector<std::string>>(value))
    {
      const std::size_t kept_len = Utf8SafePrefixLength(val, max_length);
#if defined(ENABLE_OTLP_UTF8_VALIDITY)
      if (utf8_range::IsStructurallyValid(val))
      {
        array_value->add_values()->set_string_value(val.data(), kept_len);
      }
      else
      {
        array_value->add_values()->set_bytes_value(val.data(), kept_len);
      }
#else
      array_value->add_values()->set_string_value(val.data(), kept_len);
#endif
    }
  }
}

void OtlpPopulateAttributeUtils::PopulateAttribute(
    opentelemetry::proto::common::v1::KeyValue *attribute,
    nostd::string_view key,
    const opentelemetry::common::AttributeValue &value,
    bool allow_bytes,
    std::size_t max_length) noexcept
{
  if (nullptr == attribute)
  {
    return;
  }

  // Assert size of variant to ensure that this method gets updated if the variant
  // definition changes
  static_assert(
      nostd::variant_size<opentelemetry::common::AttributeValue>::value == kAttributeValueSize,
      "AttributeValue contains unknown type");

  attribute->set_key(key.data(), key.size());
  PopulateAnyValue(attribute->mutable_value(), value, allow_bytes, max_length);
}

/** Maps from C++ attribute into OTLP proto attribute. */
void OtlpPopulateAttributeUtils::PopulateAttribute(
    opentelemetry::proto::common::v1::KeyValue *attribute,
    nostd::string_view key,
    const opentelemetry::sdk::common::OwnedAttributeValue &value,
    bool allow_bytes,
    std::size_t max_length) noexcept
{
  if (nullptr == attribute)
  {
    return;
  }

  // Assert size of variant to ensure that this method gets updated if the variant
  // definition changes
  static_assert(nostd::variant_size<opentelemetry::sdk::common::OwnedAttributeValue>::value ==
                    kOwnedAttributeValueSize,
                "OwnedAttributeValue contains unknown type");

  attribute->set_key(key.data(), key.size());
  PopulateAnyValue(attribute->mutable_value(), value, allow_bytes, max_length);
}

void OtlpPopulateAttributeUtils::PopulateAttribute(
    opentelemetry::proto::resource::v1::Resource *proto,
    const opentelemetry::sdk::resource::Resource &resource) noexcept
{
  if (nullptr == proto)
  {
    return;
  }

  for (const auto &kv : resource.GetAttributes())
  {
    OtlpPopulateAttributeUtils::PopulateAttribute(proto->add_attributes(), kv.first, kv.second,
                                                  false);
  }
}

void OtlpPopulateAttributeUtils::PopulateAttribute(
    opentelemetry::proto::common::v1::InstrumentationScope *proto,
    const opentelemetry::sdk::instrumentationscope::InstrumentationScope
        &instrumentation_scope) noexcept
{
  for (const auto &kv : instrumentation_scope.GetAttributes())
  {
    OtlpPopulateAttributeUtils::PopulateAttribute(proto->add_attributes(), kv.first, kv.second,
                                                  false);
  }
}

// The UTF-8-safe prefix algorithm now lives in the SDK common layer so the
// in-memory AttributeConverter can share it. This member is kept as a thin
// forwarder for backward compatibility with existing callers and tests.
std::size_t OtlpPopulateAttributeUtils::Utf8SafePrefixLength(const char *data,
                                                             std::size_t size,
                                                             std::size_t max_bytes) noexcept
{
  return opentelemetry::sdk::common::Utf8SafePrefixLength(data, size, max_bytes);
}

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
