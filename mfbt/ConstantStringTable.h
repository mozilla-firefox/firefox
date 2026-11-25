/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* A Constant String Table optimized for a small, fixed number of relocations.
 */

#ifndef mozilla_ConstantStringTable_h
#define mozilla_ConstantStringTable_h

#include "mozilla/Attributes.h"
#include "mozilla/Assertions.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <limits>
#include <type_traits>

namespace mozilla {

namespace detail {

// Workaround depth limit of fold expression.
consteval size_t fold(std::initializer_list<size_t> exprs) {
  size_t acc = 0;
  for (size_t e : exprs) acc += e;
  return acc;
}

}  // namespace detail

/**
 * Helper types used to store string literals in a way compatible with non-type
 * parameters from C++20. The constructor is consteval to make it clear it's
 * only to be used in constexpr context.
 *
 * Thanks to the deduction guide, one can use "some string" as a template
 * parameter of a class waiting for StringLiteral as template parameter without
 * explicitly stating the type name. In particular:
 *
 * StringLiterals<"a", "ab", "abc"> is a type that holds a list of
 * StringLiteral.
 */
template <size_t N>
struct StringLiteral {
  const std::array<char, N> data;
  MOZ_IMPLICIT consteval StringLiteral(const char (&d)[N])
      : data(as_array(d)) {}

  constexpr auto begin() const { return data.begin(); }
  constexpr auto end() const { return data.end(); }
  constexpr size_t size() const { return N; }

 private:
  static constexpr std::array<char, N> as_array(const char (&d)[N]) {
    std::array<char, N> res = {};
    std::copy(std::begin(d), std::end(d), res.begin());
    return res;
  }
};

template <size_t N>
StringLiteral(const char (&)[N]) -> StringLiteral<N>;

/**
 * Holder type for a collection of StringLiteral
 */
template <StringLiteral... S>
struct StringLiterals {
  consteval auto to_array() {
    std::array<char, detail::fold({S.size()...})> out{};
    char* out_iter = out.data();
    std::initializer_list<const char*> datas = {S.data.data()...};
    std::initializer_list<size_t> sizes = {S.size()...};
    auto sizes_iter = sizes.begin();
    for (const char* data : datas)
      out_iter = std::copy(data, data + *sizes_iter++, out_iter);
    return out;
  }
};

/**
 * An indexable collection of null-terminated strings, optimized to limit
 * relocation count. Instead of storing strings as an array of pointers, it
 * stores a single array of data (actually flattening storage), and an extra
 * array of offsets to keep track of individual string start location:
 *
 * For instance,
 *
 *  data = ["str0", "str1"]
 *
 * is actually stored as:
 *
 *  data = [ 's', 't', 'r', '0', '\0', 's', 't', 'r', '1', '\0']
 *  offsets = [0, 5]
 */
template <size_t N, size_t M>
class ConstantStringTable {
 public:
  using offset_type = std::conditional_t<
      (M <= std::numeric_limits<uint8_t>::max()), uint8_t,
      std::conditional_t<
          (M <= std::numeric_limits<uint16_t>::max()), uint16_t,
          std::conditional_t<(M <= std::numeric_limits<uint32_t>::max()),
                             uint32_t, uint64_t>>>;

  template <StringLiteral... S>
  MOZ_IMPLICIT consteval ConstantStringTable(StringLiterals<S...> s)
      : data(s.to_array()), offsets(make_offsets<S...>()) {}

  constexpr const char* operator[](size_t i) const {
    MOZ_ASSERT(i < N);
    return data.data() + offsets[i];
  }

  constexpr size_t size() const { return N; }

 private:
  const std::array<char, M> data;
  const std::array<offset_type, N> offsets;

  template <StringLiteral... S>
  static consteval auto make_offsets() {
    std::array<offset_type, sizeof...(S)> offsets = {S.size()...};
    offset_type acc = 0;
    for (size_t i = 0; i < sizeof...(S); ++i) {
      offset_type size = offsets[i];
      offsets[i] = acc;
      acc += size;
    }
    return offsets;
  }
};

template <StringLiteral... S>
  requires(sizeof...(S) > 0)
ConstantStringTable(StringLiterals<S...>)
    -> ConstantStringTable<sizeof...(S), detail::fold({S.size()...})>;

} /* namespace mozilla */

#endif /* mozilla_ConstantStringTable_h */
