// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/key_value_iterable.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/version.h"

#if OPENTELEMETRY_HAVE_EXCEPTIONS
#  include <new>
#endif

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace common
{
/**
 * A counterpart to AttributeValue that makes sure a value is owned. This
 * replaces all non-owning references with owned copies.
 *
 * The following types are not currently supported by the OpenTelemetry
 * specification, but reserved for future use:
 *  - uint64_t
 *  - std::vector<uint64_t>
 *  - std::vector<uint8_t>
 */
using OwnedAttributeValue = nostd::variant<bool,
                                           int32_t,
                                           uint32_t,
                                           int64_t,
                                           double,
                                           std::string,
                                           std::vector<bool>,
                                           std::vector<int32_t>,
                                           std::vector<uint32_t>,
                                           std::vector<int64_t>,
                                           std::vector<double>,
                                           std::vector<std::string>,
                                           uint64_t,
                                           std::vector<uint64_t>,
                                           std::vector<uint8_t>>;

enum OwnedAttributeType : std::uint8_t
{
  kTypeBool,
  kTypeInt,
  kTypeUInt,
  kTypeInt64,
  kTypeDouble,
  kTypeString,
  kTypeSpanBool,
  kTypeSpanInt,
  kTypeSpanUInt,
  kTypeSpanInt64,
  kTypeSpanDouble,
  kTypeSpanString,
  kTypeUInt64,
  kTypeSpanUInt64,
  kTypeSpanByte
};

/**
 * Byte length of the longest prefix of `data[0, size)` that fits within
 * `max_bytes` without splitting a well-formed UTF-8 multi-byte sequence. A lead
 * byte's declared length is only honored when its continuation bytes are
 * present and in range (0x80-0xBF); otherwise the lead is treated as a one-byte
 * unit, so malformed input degrades to plain byte truncation. Callers that need
 * the result to stay valid UTF-8 (string attributes, whose protobuf/JSON wire
 * form requires it) truncate at this boundary instead of cutting through a
 * multi-byte sequence.
 */
// UTF-8 lead-byte ranges used below:
//   0x00-0x7F  0xxxxxxx  ASCII, 1 byte
//   0x80-0xBF  10xxxxxx  continuation byte (never a valid lead)
//   0xC0-0xDF  110xxxxx  lead of a 2-byte sequence
//   0xE0-0xEF  1110xxxx  lead of a 3-byte sequence
//   0xF0-0xF7  11110xxx  lead of a 4-byte sequence
//   0xF8-0xFF            not a valid lead byte
inline std::size_t Utf8SafePrefixLength(const char *data,
                                        std::size_t size,
                                        std::size_t max_bytes) noexcept
{
  // When the whole value fits within the budget (including the common
  // unbounded case where max_bytes is SIZE_MAX), no truncation is possible,
  // so skip the per-byte scan.
  if (max_bytes >= size)
  {
    return size;
  }
  std::size_t i = 0;
  while (i < size)
  {
    const auto lead = static_cast<unsigned char>(data[i]);
    std::size_t seq = (lead < 0x80)   ? 1
                      : (lead < 0xC0) ? 1
                      : (lead < 0xE0) ? 2
                      : (lead < 0xF0) ? 3
                      : (lead < 0xF8) ? 4
                                      : 1;
    if (seq > 1)
    {
      if (i + seq > size)
      {
        seq = 1;
      }
      else
      {
        for (std::size_t k = 1; k < seq; ++k)
        {
          const auto continuation = static_cast<unsigned char>(data[i + k]);
          if (continuation < 0x80 || continuation > 0xBF)
          {
            seq = 1;
            break;
          }
        }
      }
    }
    if (i + seq > max_bytes)
    {
      break;
    }
    i += seq;
  }
  return i;
}

/**
 * Creates an owned copy (OwnedAttributeValue) of a non-owning AttributeValue.
 *
 * The default-constructed converter does not truncate. To apply a byte-length
 * cap during conversion, construct with the `(std::size_t max_length)`
 * overload. Only the string, string-array, and bytes alternatives are capped;
 * other alternatives are unaffected. String alternatives (std::string,
 * string_view, const char*, and arrays of them) are truncated at a UTF-8
 * code-point boundary via Utf8SafePrefixLength so the kept value stays valid
 * UTF-8 when the input was, matching the OTel convention that std::string holds
 * UTF-8 text. The bytes alternative (span<const uint8_t>) carries raw binary
 * data and is cut at the exact byte boundary with no encoding semantics.
 */
struct AttributeConverter
{
  AttributeConverter() = default;

  /// Constructs a converter that truncates string and bytes alternatives to
  /// at most `max_length` bytes during conversion. Other alternatives are
  /// unaffected. Used by recordables enforcing attribute_value_length_limit.
  explicit AttributeConverter(std::size_t max_length) noexcept : max_length_(max_length) {}

  OwnedAttributeValue operator()(bool v) { return OwnedAttributeValue(v); }
  OwnedAttributeValue operator()(int32_t v) { return OwnedAttributeValue(v); }
  OwnedAttributeValue operator()(uint32_t v) { return OwnedAttributeValue(v); }
  OwnedAttributeValue operator()(int64_t v) { return OwnedAttributeValue(v); }
  OwnedAttributeValue operator()(uint64_t v) { return OwnedAttributeValue(v); }
  OwnedAttributeValue operator()(double v) { return OwnedAttributeValue(v); }
  OwnedAttributeValue operator()(nostd::string_view v)
  {
    const std::size_t kept =
        v.size() > max_length_ ? Utf8SafePrefixLength(v.data(), v.size(), max_length_) : v.size();
    return OwnedAttributeValue(std::string(v.data(), kept));
  }
  OwnedAttributeValue operator()(std::string v)
  {
    if (v.size() > max_length_)
    {
      v.resize(Utf8SafePrefixLength(v.data(), v.size(), max_length_));
    }
    return OwnedAttributeValue(std::move(v));
  }
  OwnedAttributeValue operator()(const char *v)
  {
    // Delegate to the string_view overload so an oversized C string is not
    // copied in full before being truncated, and so it shares the UTF-8-safe
    // boundary handling.
    return (*this)(nostd::string_view(v));
  }
  OwnedAttributeValue operator()(nostd::span<const uint8_t> v)
  {
    // Raw bytes carry no encoding, so cut at the exact byte boundary.
    const std::size_t kept = (std::min)(v.size(), max_length_);
    return OwnedAttributeValue(std::vector<uint8_t>(v.data(), v.data() + kept));
  }
  OwnedAttributeValue operator()(nostd::span<const bool> v) { return convertSpan<bool>(v); }
  OwnedAttributeValue operator()(nostd::span<const int32_t> v) { return convertSpan<int32_t>(v); }
  OwnedAttributeValue operator()(nostd::span<const uint32_t> v) { return convertSpan<uint32_t>(v); }
  OwnedAttributeValue operator()(nostd::span<const int64_t> v) { return convertSpan<int64_t>(v); }
  OwnedAttributeValue operator()(nostd::span<const uint64_t> v) { return convertSpan<uint64_t>(v); }
  OwnedAttributeValue operator()(nostd::span<const double> v) { return convertSpan<double>(v); }
  OwnedAttributeValue operator()(nostd::span<const nostd::string_view> v)
  {
    std::vector<std::string> result;
    result.reserve(v.size());
    for (const auto &sv : v)
    {
      const std::size_t kept = sv.size() > max_length_
                                   ? Utf8SafePrefixLength(sv.data(), sv.size(), max_length_)
                                   : sv.size();
      result.emplace_back(sv.data(), kept);
    }
    return OwnedAttributeValue(std::move(result));
  }

  template <typename T, typename U = T>
  OwnedAttributeValue convertSpan(nostd::span<const U> vals)
  {
    return OwnedAttributeValue(std::vector<T>(vals.begin(), vals.end()));
  }

private:
  std::size_t max_length_ = (std::numeric_limits<std::size_t>::max)();
};

/**
 * Evaluates if an owned value (from an OwnedAttributeValue) is equal to another value (from a
 * non-owning AttributeValue). This only supports the checking equality with
 * nostd::visit(AttributeEqualToVisitor, OwnedAttributeValue, AttributeValue).
 */
struct AttributeEqualToVisitor
{
  // Different types are not equal including containers of different element types
  template <typename T, typename U>
  bool operator()(const T &, const U &) const noexcept
  {
    return false;
  }

  // Compare the same arithmetic types
  template <typename T>
  bool operator()(const T &owned_value, const T &value) const noexcept
  {
    return owned_value == value;
  }

  // Compare std::string and const char*
  bool operator()(const std::string &owned_value, const char *value) const noexcept
  {
    return owned_value == value;
  }

  // Compare std::string and nostd::string_view
  bool operator()(const std::string &owned_value, nostd::string_view value) const noexcept
  {
    return owned_value == value;
  }

  // Compare std::vector<std::string> and nostd::span<const nostd::string_view>
  bool operator()(const std::vector<std::string> &owned_value,
                  const nostd::span<const nostd::string_view> &value) const noexcept
  {
    return owned_value.size() == value.size() &&
           std::equal(owned_value.begin(), owned_value.end(), value.begin(),
                      [](const std::string &owned_element, nostd::string_view element) {
                        return owned_element == element;
                      });
  }

  // Compare nostd::span<const T> and std::vector<T> for arithmetic types
  template <typename T>
  bool operator()(const std::vector<T> &owned_value,
                  const nostd::span<const T> &value) const noexcept
  {
    return owned_value.size() == value.size() &&
           std::equal(owned_value.begin(), owned_value.end(), value.begin());
  }
};

/**
 * Insert or assign a key-value pair into a map using map.insert_or_assign if available, or
 * map.emplace otherwise.
 * @param map The map to insert or assign the key-value pair into.
 * @param key The key to insert or assign.
 * @param value The value to insert or assign.
 * @return A pair of an iterator to the element and a bool (true if the insertion took place).
 */
template <typename Map, typename Value>
inline std::pair<typename Map::iterator, bool>
AttributeInsertOrAssign(Map &map, opentelemetry::nostd::string_view key, Value &&value) noexcept
{
#if __cplusplus >= 201703L
  // Use insert_or_assign if C++17 is available
  return map.insert_or_assign(std::string(key), std::forward<Value>(value));
#else
  auto result          = map.emplace(std::string(key), typename Map::mapped_type{});
  result.first->second = std::forward<Value>(value);
  return result;
#endif
}

/**
 * VisitVariant
 *
 * Invokes nostd::visit(visitor, value) with exception-safe handling.
 * Returns pair<ReturnType, bool>: second=true on success, false if an exception was caught
 * On exception the first element is default-constructed.
 */
template <typename Visitor, typename Variant>
inline auto VisitVariant(Visitor &&visitor, const Variant &value) noexcept
    -> std::pair<decltype(opentelemetry::nostd::visit(std::forward<Visitor>(visitor), value)), bool>
{
#if OPENTELEMETRY_HAVE_EXCEPTIONS
  using ReturnType = decltype(opentelemetry::nostd::visit(std::forward<Visitor>(visitor), value));
  try
  {
#endif
    return {opentelemetry::nostd::visit(std::forward<Visitor>(visitor), value), true};
#if OPENTELEMETRY_HAVE_EXCEPTIONS
  }
#  if defined(OPENTELEMETRY_HAVE_STD_VARIANT)
  catch (const std::bad_variant_access &)
#  else
  catch (const opentelemetry::nostd::bad_variant_access &)
#  endif
  {
    return {ReturnType{}, false};
  }
  catch (const std::bad_alloc &)
  {
    return {ReturnType{}, false};
  }
#endif
}

/**
 * Class for storing attributes.
 */
class AttributeMap : public std::unordered_map<std::string, OwnedAttributeValue>
{
public:
  // Construct empty attribute map
  AttributeMap() : std::unordered_map<std::string, OwnedAttributeValue>() {}

  // Construct attribute map and populate with attributes
  AttributeMap(const opentelemetry::common::KeyValueIterable &attributes) : AttributeMap()
  {
    ConstructFrom(attributes);
  }

  // Construct attribute map and populate with optional attributes
  AttributeMap(const opentelemetry::common::KeyValueIterable *attributes) : AttributeMap()
  {
    if (attributes != nullptr)
    {
      ConstructFrom(*attributes);
    }
  }

  // Construct map from initializer list by applying `SetAttribute` transform for every attribute
  AttributeMap(
      std::initializer_list<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>>
          attributes)
      : AttributeMap()
  {
    for (auto &kv : attributes)
    {
      SetAttribute(kv.first, kv.second);
    }
  }

  void ConstructFrom(const opentelemetry::common::KeyValueIterable &attributes)
  {
    attributes.ForEachKeyValue(
        [&](nostd::string_view key, const opentelemetry::common::AttributeValue &value) noexcept {
          SetAttribute(key, value);
          return true;
        });
  }

  // Returns a reference to this map
  const std::unordered_map<std::string, OwnedAttributeValue> &GetAttributes() const noexcept
  {
    return (*this);
  }

  // Convert non-owning key-value to owning std::string(key) and OwnedAttributeValue(value)
  bool SetAttribute(nostd::string_view key,
                    const opentelemetry::common::AttributeValue &value,
                    std::size_t max_length = (std::numeric_limits<std::size_t>::max)()) noexcept
  {
    std::pair<OwnedAttributeValue, bool> result =
        VisitVariant(AttributeConverter(max_length), value);
    if (result.second)
    {
      AttributeInsertOrAssign(*this, key, std::move(result.first));
      return true;
    }
    return false;
  }

  // Compare the attributes of this map with another KeyValueIterable
  bool EqualTo(const opentelemetry::common::KeyValueIterable &attributes) const noexcept
  {
    if (attributes.size() != this->size())
    {
      return false;
    }

    const bool is_equal = attributes.ForEachKeyValue(
        [this](nostd::string_view key,
               const opentelemetry::common::AttributeValue &value) noexcept {
          // Perform a linear search to find the key assuming the map is small
          // This avoids temporary string creation from this->find(std::string(key))
          for (const auto &kv : *this)
          {
            if (kv.first == key)
            {
              // Order of arguments is important here. OwnedAttributeValue is first then
              // AttributeValue AttributeEqualToVisitor does not support the reverse order
              return nostd::visit(AttributeEqualToVisitor(), kv.second, value);
            }
          }
          return false;
        });

    return is_equal;
  }
};

/**
 * Class for storing attributes.
 */
class OrderedAttributeMap : public std::map<std::string, OwnedAttributeValue>
{
public:
  // Construct empty attribute map
  OrderedAttributeMap() : std::map<std::string, OwnedAttributeValue>() {}

  // Construct attribute map and populate with attributes
  OrderedAttributeMap(const opentelemetry::common::KeyValueIterable &attributes)
      : OrderedAttributeMap()
  {
    attributes.ForEachKeyValue(
        [&](nostd::string_view key, opentelemetry::common::AttributeValue value) noexcept {
          SetAttribute(key, value);
          return true;
        });
  }

  // Construct map from initializer list by applying `SetAttribute` transform for every attribute
  OrderedAttributeMap(
      std::initializer_list<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>>
          attributes)
      : OrderedAttributeMap()
  {
    for (auto &kv : attributes)
    {
      SetAttribute(kv.first, kv.second);
    }
  }

  // Returns a reference to this map
  const std::map<std::string, OwnedAttributeValue> &GetAttributes() const noexcept
  {
    return (*this);
  }

  // Convert non-owning key-value to owning std::string(key) and OwnedAttributeValue(value)
  void SetAttribute(nostd::string_view key,
                    const opentelemetry::common::AttributeValue &value,
                    std::size_t max_length = (std::numeric_limits<std::size_t>::max)()) noexcept
  {
    std::pair<OwnedAttributeValue, bool> result =
        VisitVariant(AttributeConverter(max_length), value);
    if (result.second)
    {
      AttributeInsertOrAssign(*this, key, std::move(result.first));
    }
  }
};

}  // namespace common
}  // namespace sdk
OPENTELEMETRY_END_NAMESPACE
