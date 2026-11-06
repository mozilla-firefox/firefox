// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "opentelemetry/common/detail/ascii.h"
#include "opentelemetry/common/kv_properties.h"
#include "opentelemetry/nostd/function_ref.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/nostd/unique_ptr.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace trace
{

/**
 * TraceState carries tracing-system specific context in a list of key-value pairs. TraceState
 * allows different vendors to propagate additional information and inter-operate with their legacy
 * id formats.
 *
 * For more information, see the W3C Trace Context specification:
 * https://www.w3.org/TR/trace-context
 */
class OPENTELEMETRY_EXPORT TraceState
{
public:
  static constexpr int kKeyMaxSize         = 256;
  static constexpr int kValueMaxSize       = 256;
  static constexpr int kMaxKeyValuePairs   = 32;
  static constexpr auto kKeyValueSeparator = '=';
  static constexpr auto kMembersSeparator  = ',';

  OPENTELEMETRY_API_SINGLETON static nostd::shared_ptr<TraceState> GetDefault()
  {
    static nostd::shared_ptr<TraceState> ts{new TraceState()};
    return ts;
  }

  /**
   * Returns shared_ptr to a newly created TraceState parsed from the header provided.
   * @param header Encoding of the tracestate header defined by
   * the W3C Trace Context specification https://www.w3.org/TR/trace-context/
   * @return TraceState A new TraceState instance or DEFAULT
   */
  static nostd::shared_ptr<TraceState> FromHeader(nostd::string_view header) noexcept
  {

    common::KeyValueStringTokenizer kv_str_tokenizer(header);
    size_t cnt = kv_str_tokenizer.NumTokens();  // upper bound on number of kv pairs
    if (cnt > kMaxKeyValuePairs)
    {
      // trace state should be discarded if count exceeds
      return GetDefault();
    }

    nostd::shared_ptr<TraceState> ts(new TraceState(cnt));
    bool kv_valid{false};
    nostd::string_view key, value;
    while (kv_str_tokenizer.next(kv_valid, key, value) && ts->kv_properties_->Size() < cnt)
    {
      if (kv_valid == false)
      {
        return GetDefault();
      }

      if (!IsValidKey(key) || !IsValidValue(value))
      {
        // invalid header. return empty TraceState
        return GetDefault();
      }

      // W3C trace-context tests require tracestate to be populated when it contains
      // duplicate keys. Discard duplicate entries.
      // https://github.com/w3c/trace-context/blob/acab820be9db7b3433668baa5cdd43f57f4c4be0/test/test.py#L565
      if (!ts->kv_properties_->HasKey(key))
      {
        ts->kv_properties_->AddEntry(key, value);
      }
    }

    return ts;
  }

  /**
   * Creates a w3c tracestate header from TraceState object
   */
  std::string ToHeader() const noexcept
  {
    std::string header_s;
    bool first = true;
    kv_properties_->GetAllEntries(
        [&header_s, &first](nostd::string_view key, nostd::string_view value) noexcept {
          if (!first)
          {
            header_s.append(1, kMembersSeparator);
          }
          else
          {
            first = false;
          }
          header_s.append(key.data(), key.size());
          header_s.append(1, kKeyValueSeparator);
          header_s.append(value.data(), value.size());
          return true;
        });
    return header_s;
  }

  /**
   *  Returns `value` associated with `key` passed as argument
   *  Returns empty string if key is invalid  or not found
   */
  bool Get(nostd::string_view key, std::string &value) const noexcept
  {
    if (!IsValidKey(key))
    {
      return false;
    }

    return kv_properties_->GetValue(key, value);
  }

  /**
   * Returns shared_ptr of `new` TraceState object with following mutations applied to the existing
   * instance: Update Key value: The updated value must be moved to beginning of List Add : The new
   * key-value pair SHOULD be added to beginning of List
   *
   * If the provided key-value pair is invalid, or results in transtate that violates the
   * tracecontext specification, empty TraceState instance will be returned.
   *
   * If the existing object has maximum list members and the key is not already present, then the
   * new key-value pair is ignored and a copy of the existing TraceState is returned. If an entry
   * with the same key is present then the existing entry will be replaced with the updated
   * key-value pair at the beginning of the list.
   */
  nostd::shared_ptr<TraceState> Set(const nostd::string_view &key,
                                    const nostd::string_view &value) noexcept
  {
    if (!IsValidKey(key) || !IsValidValue(value))
    {
      // max size reached or invalid key/value. Returning empty TraceState
      return TraceState::GetDefault();
    }
    const size_t curr_size = kv_properties_->Size();
    const bool at_capacity = curr_size >= kMaxKeyValuePairs;

    const bool replacing_at_capacity = at_capacity && kv_properties_->HasKey(key);
    size_t allocate_size             = curr_size;

    if (!at_capacity)
    {
      allocate_size += 1;
    }
    nostd::shared_ptr<TraceState> ts(new TraceState(allocate_size));

    if (!at_capacity || replacing_at_capacity)
    {
      // add the new or replacement entry first
      ts->kv_properties_->AddEntry(key, value);
    }
    // add rest of the fields, excluding the old entry for `key` so it isn't duplicated.
    // Keys are unique, so at most one existing entry can match `key`. Once we've found
    // (or already know there isn't) a match, skip comparing the rest.
    bool skip_key_check = at_capacity && !replacing_at_capacity;
    kv_properties_->GetAllEntries(
        [&ts, &key, &skip_key_check](nostd::string_view e_key, nostd::string_view e_value) {
          if (skip_key_check || e_key != key)
          {
            ts->kv_properties_->AddEntry(e_key, e_value);
          }
          else
          {
            skip_key_check = true;
          }
          return true;
        });
    return ts;
  }

  /**
   * Returns shared_ptr to a `new` TraceState object after removing the attribute with given key (
   * if present )
   * @returns empty TraceState object if key is invalid
   * @returns copy of original TraceState object if key is not present (??)
   */
  nostd::shared_ptr<TraceState> Delete(const nostd::string_view &key) noexcept
  {
    if (!IsValidKey(key))
    {
      return TraceState::GetDefault();
    }
    const size_t curr_size     = kv_properties_->Size();
    const bool has_key         = kv_properties_->HasKey(key);
    const size_t allocate_size = has_key ? curr_size - 1 : curr_size;
    nostd::shared_ptr<TraceState> ts(new TraceState(allocate_size));
    // Keys are unique, so at most one existing entry can match `key`. Once we've found it,
    // skip comparing the rest.
    bool skip_key_check = !has_key;
    kv_properties_->GetAllEntries(
        [&ts, &key, &skip_key_check](nostd::string_view e_key, nostd::string_view e_value) {
          if (skip_key_check || key != e_key)
          {
            ts->kv_properties_->AddEntry(e_key, e_value);
          }
          else
          {
            skip_key_check = true;
          }
          return true;
        });
    return ts;
  }

  // Returns true if there are no keys, false otherwise.
  bool Empty() const noexcept { return kv_properties_->Size() == 0; }

  // @return all key-values entris by repeatedly invoking the function reference passed as argument
  // for each entry
  bool GetAllEntries(
      nostd::function_ref<bool(nostd::string_view, nostd::string_view)> callback) const noexcept
  {
    return kv_properties_->GetAllEntries(callback);
  }
  /** Returns whether key is a valid key. See https://www.w3.org/TR/trace-context/#key
   * Identifiers MUST begin with a lowercase letter or a digit, and can only contain
   * lowercase letters (a-z), digits (0-9), underscores (_), dashes (-), asterisks (*),
   * forward slashes (/), and at signs (@).
   * An at sign (@) is treated as a regular character (keychar) with no structural meaning.
   * Total key length must not exceed 256 characters.
   */
  static bool IsValidKey(nostd::string_view key) noexcept { return IsValidKeyNonRegEx(key); }

  /** Returns whether value is a valid value. See https://www.w3.org/TR/trace-context/#value
   * The value is an opaque string containing up to 256 printable ASCII (RFC0020)
   *  characters ((i.e., the range 0x20 to 0x7E) except comma , and equal =)
   */
  static bool IsValidValue(nostd::string_view value) noexcept
  {
    return IsValidValueNonRegEx(value);
  }

private:
  TraceState() : kv_properties_(new common::KeyValueProperties()) {}
  TraceState(size_t size) : kv_properties_(new common::KeyValueProperties(size)) {}

  static nostd::string_view TrimString(nostd::string_view str, size_t left, size_t right)
  {
    while (str[static_cast<std::size_t>(right)] == ' ' && left < right)
    {
      right--;
    }
    while (str[static_cast<std::size_t>(left)] == ' ' && left < right)
    {
      left++;
    }
    return str.substr(left, right - left + 1);
  }

  static bool IsValidKeyNonRegEx(nostd::string_view key) noexcept
  {
    if (key.empty() || key.size() > kKeyMaxSize || !IsLowerCaseAlphaOrDigit(key[0]))
    {
      return false;
    }

    for (const char c : key)
    {
      if (!IsLowerCaseAlphaOrDigit(c) && c != '_' && c != '-' && c != '@' && c != '*' && c != '/')
      {
        return false;
      }
    }
    return true;
  }

  static bool IsValidValueNonRegEx(nostd::string_view value) noexcept
  {
    if (value.empty() || value.size() > kValueMaxSize)
    {
      return false;
    }

    for (const char c : value)
    {
      if (c < ' ' || c > '~' || c == ',' || c == '=')
      {
        return false;
      }
    }
    return true;
  }

  static bool IsLowerCaseAlphaOrDigit(char c) noexcept
  {
    return common::detail::IsAsciiDigit(c) || common::detail::IsAsciiLower(c);
  }

private:
  // Store entries in a C-style array to avoid using std::array or std::vector.
  nostd::unique_ptr<common::KeyValueProperties> kv_properties_;
};

}  // namespace trace
OPENTELEMETRY_END_NAMESPACE
