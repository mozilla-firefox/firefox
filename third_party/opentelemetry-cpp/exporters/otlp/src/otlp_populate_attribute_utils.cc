// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#if defined(ENABLE_OTLP_UTF8_VALIDITY)
#  include <utf8_validity.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/exporters/otlp/otlp_populate_attribute_utils.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/version.h"

// clang-format off
#include "opentelemetry/exporters/otlp/protobuf_include_prefix.h" // IWYU pragma: keep
// IWYU pragma: no_include <google/protobuf/repeated_ptr_field.h>
// IWYU pragma: no_include "net/proto2/public/repeated_field.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"
#include "opentelemetry/exporters/otlp/protobuf_include_suffix.h" // IWYU pragma: keep
// clang-format on

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

namespace
{

//
// See `attribute_value.h` for details.
//
constexpr std::size_t kAttributeValueSize      = 16;
constexpr std::size_t kOwnedAttributeValueSize = 15;

// Per OpenTelemetry spec, std::uint64_t attribute values exceeding INT64_MAX must be
// encoded as a decimal string rather than wrapping to a negative int64 via narrowing.
// https://opentelemetry.io/docs/specs/otel/common/attribute-type-mapping/#integer-values
inline void SetUint64Value(opentelemetry::proto::common::v1::AnyValue *proto_value,
                           std::uint64_t val)
{
  if (val <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
  {
    proto_value->set_int_value(static_cast<std::int64_t>(val));
  }
  else
  {
    proto_value->set_string_value(std::to_string(val));
  }
}

template <class T>
struct IsSupportedAttributeValue : std::false_type
{};

struct AttributeValueVisitor
{
  opentelemetry::proto::common::v1::AnyValue *proto_value_;
  AttributeConverterOptions options;

  template <class T>
  void operator()(T &&) const
  {
    static_assert(
        IsSupportedAttributeValue<T>::value,
        "AttributeValueVisitor: Value type in opentelemetry::common::AttributeValue does not have "
        "an overload operator implemented in this visitor OR implicit conversion attempted!");
  }

  void operator()(bool value) const { proto_value_->set_bool_value(value); }
  void operator()(std::int32_t value) const { proto_value_->set_int_value(value); }
  void operator()(std::int64_t value) const { proto_value_->set_int_value(value); }
  void operator()(std::uint32_t value) const { proto_value_->set_int_value(value); }
  void operator()(std::uint64_t value) const { SetUint64Value(proto_value_, value); }
  void operator()(double value) const { proto_value_->set_double_value(value); }
  void operator()(const char *value) const
  {
    operator()(nostd::string_view{value, std::strlen(value)});
  }
  void operator()(nostd::string_view value) const
  {
    const std::size_t kept_len = OtlpPopulateAttributeUtils::Utf8SafePrefixLength(
        value.data(), value.size(), options.attribute_value_length_limit);
#if defined(ENABLE_OTLP_UTF8_VALIDITY)
    if (utf8_range::IsStructurallyValid({value.data(), kept_len}))
    {
      proto_value_->set_string_value(value.data(), kept_len);
    }
    else
    {
      proto_value_->set_bytes_value(value.data(), kept_len);
    }
#else
    proto_value_->set_string_value(value.data(), kept_len);
#endif
  }
  void operator()(nostd::span<const bool> values) const
  {
    opentelemetry::proto::common::v1::ArrayValue *array_value = proto_value_->mutable_array_value();
    array_value->mutable_values()->Reserve(static_cast<int>(values.size()));
    for (const bool val : values)
    {
      array_value->add_values()->set_bool_value(val);
    }
  }
  void operator()(nostd::span<const std::int32_t> values) const
  {
    opentelemetry::proto::common::v1::ArrayValue *array_value = proto_value_->mutable_array_value();
    array_value->mutable_values()->Reserve(static_cast<int>(values.size()));
    for (const std::int32_t val : values)
    {
      array_value->add_values()->set_int_value(val);
    }
  }
  void operator()(nostd::span<const std::int64_t> values) const
  {
    opentelemetry::proto::common::v1::ArrayValue *array_value = proto_value_->mutable_array_value();
    array_value->mutable_values()->Reserve(static_cast<int>(values.size()));
    for (const std::int64_t val : values)
    {
      array_value->add_values()->set_int_value(val);
    }
  }
  void operator()(nostd::span<const std::uint32_t> values) const
  {
    opentelemetry::proto::common::v1::ArrayValue *array_value = proto_value_->mutable_array_value();
    array_value->mutable_values()->Reserve(static_cast<int>(values.size()));
    for (const std::uint32_t val : values)
    {
      array_value->add_values()->set_int_value(val);
    }
  }
  void operator()(nostd::span<const double> values) const
  {
    opentelemetry::proto::common::v1::ArrayValue *array_value = proto_value_->mutable_array_value();
    array_value->mutable_values()->Reserve(static_cast<int>(values.size()));
    for (const double val : values)
    {
      array_value->add_values()->set_double_value(val);
    }
  }
  void operator()(nostd::span<const nostd::string_view> values) const
  {
    opentelemetry::proto::common::v1::ArrayValue *array_value = proto_value_->mutable_array_value();
    array_value->mutable_values()->Reserve(static_cast<int>(values.size()));
    for (const nostd::string_view val : values)
    {
      const std::size_t kept_len = OtlpPopulateAttributeUtils::Utf8SafePrefixLength(
          val.data(), val.size(), options.attribute_value_length_limit);
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
  void operator()(nostd::span<const std::uint64_t> values) const
  {
    opentelemetry::proto::common::v1::ArrayValue *array_value = proto_value_->mutable_array_value();
    array_value->mutable_values()->Reserve(static_cast<int>(values.size()));
    for (const std::uint64_t val : values)
    {
      SetUint64Value(array_value->add_values(), val);
    }
  }
  void operator()(nostd::span<const std::uint8_t> values) const
  {
    const std::size_t kept_len = (std::min)(values.size(), options.attribute_value_length_limit);
    proto_value_->set_bytes_value(reinterpret_cast<const void *>(values.data()), kept_len);
  }
};

struct OwnedAttributeValueVisitor
{
  opentelemetry::proto::common::v1::AnyValue *proto_value_;
  AttributeConverterOptions options;

  template <class T>
  void operator()(T &&) const
  {
    static_assert(IsSupportedAttributeValue<T>::value,
                  "OwnedAttributeValueVisitor: Value type in "
                  "opentelemetry::sdk::common::OwnedAttributeValue does not have an overload "
                  "operator implemented in this visitor OR implicit conversion attempted!");
  }

  void operator()(bool value) const { proto_value_->set_bool_value(value); }
  void operator()(std::int32_t value) const { proto_value_->set_int_value(value); }
  void operator()(std::uint32_t value) const { proto_value_->set_int_value(value); }
  void operator()(std::int64_t value) const { proto_value_->set_int_value(value); }
  void operator()(std::uint64_t value) const { SetUint64Value(proto_value_, value); }
  void operator()(double value) const { proto_value_->set_double_value(value); }
  void operator()(const std::string &value) const
  {
    const std::size_t kept_len = OtlpPopulateAttributeUtils::Utf8SafePrefixLength(
        value.data(), value.size(), options.attribute_value_length_limit);
#if defined(ENABLE_OTLP_UTF8_VALIDITY)
    if (utf8_range::IsStructurallyValid({value.data(), kept_len}))
    {
      proto_value_->set_string_value(value.data(), kept_len);
    }
    else
    {
      proto_value_->set_bytes_value(value.data(), kept_len);
    }
#else
    proto_value_->set_string_value(value.data(), kept_len);
#endif
  }
  void operator()(const std::vector<bool> &values) const
  {
    opentelemetry::proto::common::v1::ArrayValue *array_value = proto_value_->mutable_array_value();
    array_value->mutable_values()->Reserve(static_cast<int>(values.size()));
    for (const bool val : values)
    {
      array_value->add_values()->set_bool_value(val);
    }
  }
  void operator()(const std::vector<std::int32_t> &values) const
  {
    opentelemetry::proto::common::v1::ArrayValue *array_value = proto_value_->mutable_array_value();
    array_value->mutable_values()->Reserve(static_cast<int>(values.size()));
    for (const std::int32_t val : values)
    {
      array_value->add_values()->set_int_value(val);
    }
  }
  void operator()(const std::vector<std::uint32_t> &values) const
  {
    opentelemetry::proto::common::v1::ArrayValue *array_value = proto_value_->mutable_array_value();
    array_value->mutable_values()->Reserve(static_cast<int>(values.size()));
    for (const std::uint32_t val : values)
    {
      array_value->add_values()->set_int_value(val);
    }
  }
  void operator()(const std::vector<std::int64_t> &values) const
  {
    opentelemetry::proto::common::v1::ArrayValue *array_value = proto_value_->mutable_array_value();
    array_value->mutable_values()->Reserve(static_cast<int>(values.size()));
    for (const std::int64_t val : values)
    {
      array_value->add_values()->set_int_value(val);
    }
  }
  void operator()(const std::vector<double> &values) const
  {
    opentelemetry::proto::common::v1::ArrayValue *array_value = proto_value_->mutable_array_value();
    array_value->mutable_values()->Reserve(static_cast<int>(values.size()));
    for (const double val : values)
    {
      array_value->add_values()->set_double_value(val);
    }
  }
  void operator()(const std::vector<std::string> &values) const
  {
    opentelemetry::proto::common::v1::ArrayValue *array_value = proto_value_->mutable_array_value();
    array_value->mutable_values()->Reserve(static_cast<int>(values.size()));
    for (const std::string &val : values)
    {
      const std::size_t kept_len = OtlpPopulateAttributeUtils::Utf8SafePrefixLength(
          val, options.attribute_value_length_limit);
#if defined(ENABLE_OTLP_UTF8_VALIDITY)
      if (utf8_range::IsStructurallyValid({val.data(), kept_len}))
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
  void operator()(const std::vector<std::uint64_t> &values) const
  {
    opentelemetry::proto::common::v1::ArrayValue *array_value = proto_value_->mutable_array_value();
    array_value->mutable_values()->Reserve(static_cast<int>(values.size()));
    for (const std::uint64_t val : values)
    {
      SetUint64Value(array_value->add_values(), val);
    }
  }
  void operator()(const std::vector<std::uint8_t> &values) const
  {
    const std::size_t kept_len = (std::min)(values.size(), options.attribute_value_length_limit);
    proto_value_->set_bytes_value(reinterpret_cast<const void *>(values.data()), kept_len);
  }
};

}  // namespace

bool OtlpPopulateAttributeUtils::PopulateAnyValue(
    opentelemetry::proto::common::v1::AnyValue *proto_value,
    const opentelemetry::common::AttributeValue &value,
    AttributeConverterOptions options) noexcept
{
  auto result =
      opentelemetry::sdk::common::VisitVariant(AttributeValueVisitor{proto_value, options}, value);
  return result.second;
}

bool OtlpPopulateAttributeUtils::PopulateAnyValue(
    opentelemetry::proto::common::v1::AnyValue *proto_value,
    const opentelemetry::sdk::common::OwnedAttributeValue &value,
    AttributeConverterOptions options) noexcept
{
  auto result = opentelemetry::sdk::common::VisitVariant(
      OwnedAttributeValueVisitor{proto_value, options}, value);
  return result.second;
}

void OtlpPopulateAttributeUtils::PopulateAttribute(
    opentelemetry::proto::common::v1::KeyValue *attribute,
    nostd::string_view key,
    const opentelemetry::common::AttributeValue &value,
    AttributeConverterOptions options) noexcept
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
  bool result = PopulateAnyValue(attribute->mutable_value(), value, options);
  if (!result)
  {
    OTEL_INTERNAL_LOG_ERROR(
        "[OTLP Populate Attribute] PopulateAnyValue from AttributeValue failed for key: "
        << std::string(key));
  }
}

/** Maps from C++ attribute into OTLP proto attribute. */
void OtlpPopulateAttributeUtils::PopulateAttribute(
    opentelemetry::proto::common::v1::KeyValue *attribute,
    nostd::string_view key,
    const opentelemetry::sdk::common::OwnedAttributeValue &value,
    AttributeConverterOptions options) noexcept
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
  bool result = PopulateAnyValue(attribute->mutable_value(), value, options);
  if (!result)
  {
    OTEL_INTERNAL_LOG_ERROR(
        "[OTLP Populate Attribute] PopulateAnyValue from OwnedAttributeValue failed for key: "
        << std::string(key));
  }
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
                                                  AttributeConverterOptions{});
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
                                                  AttributeConverterOptions{});
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
