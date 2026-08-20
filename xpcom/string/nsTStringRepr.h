/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsTStringRepr_h
#define nsTStringRepr_h

#include <limits>
#include <string_view>
#include <type_traits>  // std::enable_if

#include "fmt/format.h"
#include "fmt/xchar.h"
#include "mozilla/Char16.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/StringBuffer.h"
#include "mozilla/fallible.h"
#include "nsCharTraits.h"
#include "nsStringFlags.h"
#include "nsStringFwd.h"
#include "nsStringIterator.h"

template <typename T>
class nsTSubstringTuple;

namespace mozilla {

// This is mainly intended to be used in the context of nsTStrings where
// we want to enable a specific function only for a given character class. In
// order for this technique to work the member function needs to be templated
// on something other than `T`. We keep this in the `mozilla` namespace rather
// than `nsTStringRepr` as it's intentionally not dependent on `T`.
//
// The 'T' at the end of `Char[16]OnlyT` is refering to the `::type` portion
// which will only be defined if the character class is correct. This is similar
// to `std::enable_if_t` which is available in C++14, but not C++11.
//
// `CharType` is generally going to be a shadowed type of `T`.
//
// Example usage of a function that will only be defined if `T` == `char`:
//
// template <typename T>
// class nsTSubstring : public nsTStringRepr<T> {
//   template <typename Q = T, typename EnableForChar = typename CharOnlyT<Q>>
//   int Foo() { return 42; }
// };
//
// Please note that we had to use a separate type `Q` for this to work. You
// will get a semi-decent compiler error if you use `T` directly.

template <typename CharType>
using CharOnlyT = std::enable_if_t<std::is_same_v<char, CharType>>;

template <typename CharType>
using Char16OnlyT = std::enable_if_t<std::is_same_v<char16_t, CharType>>;

namespace detail {

// nsTStringLengthStorage is a helper class which holds the string's length and
// provides getters and setters for converting to and from `size_t`. This is
// done to allow the length to be stored in a `uint32_t` using assertions.
template <typename T>
class nsTStringLengthStorage {
 public:
  // The maximum byte capacity for a `nsTString` must fit within an `int32_t`,
  // with enough room for a trailing null, as consumers often cast `Length()`
  // and `Capacity()` to smaller types like `int32_t`.
  static constexpr size_t kMax =
      size_t{std::numeric_limits<int32_t>::max()} / sizeof(T) - 1;
  static_assert(
      (kMax + 1) * sizeof(T) <= std::numeric_limits<int32_t>::max(),
      "nsTString's maximum length, including the trailing null, must fit "
      "within `int32_t`, as callers will cast to `int32_t` occasionally");
  static_assert(((CheckedInt<uint32_t>{kMax} + 1) * sizeof(T) +
                 sizeof(mozilla::StringBuffer))
                    .isValid(),
                "Math required to allocate a mozilla::StringBuffer for a "
                "maximum-capacity string must not overflow uint32_t");

  // Implicit conversion and assignment from `size_t` which assert that the
  // value is in-range.
  MOZ_IMPLICIT constexpr nsTStringLengthStorage(size_t aLength)
      : mLength(static_cast<uint32_t>(aLength)) {
    MOZ_RELEASE_ASSERT(aLength <= kMax, "string is too large");
  }
  constexpr nsTStringLengthStorage& operator=(size_t aLength) {
    MOZ_RELEASE_ASSERT(aLength <= kMax, "string is too large");
    mLength = static_cast<uint32_t>(aLength);
    return *this;
  }
  MOZ_IMPLICIT constexpr operator size_t() const { return mLength; }

 private:
  uint32_t mLength = 0;
};

// nsTStringRepr defines a string's memory layout and some accessor methods.
// This class exists so that nsTLiteralString can avoid inheriting
// nsTSubstring's destructor. All methods on this class must be const because
// literal strings are not writable.
//
// This class is an implementation detail and should not be instantiated
// directly, nor used in any way outside of the string code itself. It is
// buried in a namespace to discourage its use in function parameters.
// If you need to take a parameter, use [const] ns[C]Substring&.
// If you need to instantiate a string, use ns[C]String or descendents.
//
// NAMES:
//   nsStringRepr for wide characters
//   nsCStringRepr for narrow characters
template <typename T>
class nsTStringRepr {
 public:
  typedef mozilla::fallible_t fallible_t;

  typedef T char_type;

  typedef nsCharTraits<char_type> char_traits;
  typedef typename char_traits::incompatible_char_type incompatible_char_type;

  typedef nsTStringRepr<T> self_type;
  typedef self_type base_string_type;

  typedef nsTSubstring<T> substring_type;
  typedef nsTSubstringTuple<T> substring_tuple_type;

  typedef nsReadingIterator<char_type> const_iterator;
  typedef char_type* iterator;

  typedef nsTStringComparator<char_type> comparator_type;

  typedef const char_type* const_char_iterator;

  typedef std::basic_string_view<char_type> string_view;

  typedef size_t index_type;
  typedef size_t size_type;

  // These are only for internal use within the string classes:
  typedef StringDataFlags DataFlags;
  typedef StringClassFlags ClassFlags;
  typedef nsTStringLengthStorage<T> LengthStorage;

  nsTStringRepr() = delete;  // Never instantiate directly

  // Reading iterators.
  // You must not assume the returned value is null-terminated.
  // If you need a null terminated string, use nsTString::get().
  constexpr const_char_iterator BeginReading() const MOZ_LIFETIME_BOUND
      MOZ_NON_TERMINATED_STRING {
    return mData;
  }
  constexpr const_char_iterator EndReading() const MOZ_LIFETIME_BOUND {
    return mData + mLength;
  }

  // Deprecated reading iterators.
  const_iterator& BeginReading(const_iterator& aIter) const MOZ_LIFETIME_BOUND {
    aIter.mStart = mData;
    aIter.mEnd = mData + mLength;
    aIter.mPosition = aIter.mStart;
    return aIter;
  }

  const_iterator& EndReading(const_iterator& aIter) const MOZ_LIFETIME_BOUND {
    aIter.mStart = mData;
    aIter.mEnd = mData + mLength;
    aIter.mPosition = aIter.mEnd;
    return aIter;
  }

  const_char_iterator& BeginReading(const_char_iterator& aIter) const
      MOZ_LIFETIME_BOUND MOZ_NON_TERMINATED_STRING {
    return aIter = mData;
  }

  const_char_iterator& EndReading(const_char_iterator& aIter) const
      MOZ_LIFETIME_BOUND {
    return aIter = mData + mLength;
  }

  // Accessors.
  template <typename U, typename Dummy>
  struct raw_type {
    typedef const U* type;
  };
#if defined(MOZ_USE_CHAR16_WRAPPER)
  template <typename Dummy>
  struct raw_type<char16_t, Dummy> {
    typedef char16ptr_t type;
  };
#endif

  // Returns a raw pointer to the start of the string's data.
  // You must not assume the returned value is null-terminated.
  // If you need a null terminated string, use nsTString::get().
  constexpr typename raw_type<T, int>::type Data() const MOZ_LIFETIME_BOUND
      MOZ_NON_TERMINATED_STRING {
    return mData;
  }

  constexpr size_type Length() const { return static_cast<size_type>(mLength); }

  constexpr string_view View() const MOZ_LIFETIME_BOUND {
    return string_view(Data(), Length());
  }

  constexpr operator string_view() const MOZ_LIFETIME_BOUND { return View(); }

  constexpr DataFlags GetDataFlags() const { return mDataFlags; }

  constexpr bool IsEmpty() const { return mLength == 0; }

  constexpr bool IsLiteral() const {
    return !!(mDataFlags & DataFlags::LITERAL);
  }

  constexpr bool IsVoid() const { return !!(mDataFlags & DataFlags::VOIDED); }

  constexpr bool IsTerminated() const {
    return !!(mDataFlags & DataFlags::TERMINATED);
  }

  constexpr char_type CharAt(index_type aIndex) const {
    MOZ_ASSERT(aIndex < Length(), "index exceeds allowable range");
    return mData[aIndex];
  }

  constexpr char_type operator[](index_type aIndex) const {
    return CharAt(aIndex);
  }

  char_type First() const;

  char_type Last() const;

  // Equality.
  bool NS_FASTCALL Equals(const self_type&) const;
  bool NS_FASTCALL Equals(const self_type&, comparator_type) const;

  bool NS_FASTCALL Equals(const substring_tuple_type& aTuple) const;
  bool NS_FASTCALL Equals(const substring_tuple_type& aTuple,
                          comparator_type) const;

  bool NS_FASTCALL Equals(const char_type* aData) const;
  bool NS_FASTCALL Equals(const char_type* aData, comparator_type) const;

  /**
   * Compare this string and another ASCII-case-insensitively.
   *
   * This method is similar to `LowerCaseEqualsASCII` however both strings are
   * lowercased, meaning that `aString` need not be all lowercase.
   *
   * @param   aString is the string to check
   * @return  boolean
   */
  bool EqualsIgnoreCase(const std::string_view& aString) const;

#if defined(MOZ_USE_CHAR16_WRAPPER)
  template <typename Q = T, typename EnableIfChar16 = Char16OnlyT<Q>>
  bool NS_FASTCALL Equals(char16ptr_t aData) const {
    return Equals(static_cast<const char16_t*>(aData));
  }
  template <typename Q = T, typename EnableIfChar16 = Char16OnlyT<Q>>
  bool NS_FASTCALL Equals(char16ptr_t aData, comparator_type aComp) const {
    return Equals(static_cast<const char16_t*>(aData), aComp);
  }
#endif

  // An efficient comparison with ASCII that can be used even
  // for wide strings. Call this version when you know the
  // length of 'data'.
  bool NS_FASTCALL EqualsASCII(const char* aData, size_type aLen) const;
  // An efficient comparison with ASCII that can be used even
  // for wide strings. Call this version when 'data' is
  // null-terminated.
  bool NS_FASTCALL EqualsASCII(const char* aData) const;

  // An efficient comparison with Latin1 characters that can be used even for
  // wide strings.
  bool EqualsLatin1(const char* aData, size_type aLength) const;

  // EqualsLiteral must ONLY be called with an actual literal string, or
  // a char array *constant* declared without an explicit size and with an
  // initializer that is a string literal or is otherwise null-terminated.
  // Use EqualsASCII for other char array variables.
  // (Although this method may happen to produce expected results for other
  // char arrays that have bound one greater than the sequence of interest,
  // such use is discouraged for reasons of readability and maintainability.)
  // The template trick to acquire the array bound at compile time without
  // using a macro is due to Corey Kosak, with much thanks.
  template <int N>
  inline bool EqualsLiteral(const char (&aStr)[N]) const {
    return EqualsASCII(aStr, N - 1);
  }

  // EqualsLiteral must ONLY be called with an actual literal string, or
  // a char array *constant* declared without an explicit size and with an
  // initializer that is a string literal or is otherwise null-terminated.
  // Use EqualsASCII for other char array variables.
  // (Although this method may happen to produce expected results for other
  // char arrays that have bound one greater than the sequence of interest,
  // such use is discouraged for reasons of readability and maintainability.)
  // The template trick to acquire the array bound at compile time without
  // using a macro is due to Corey Kosak, with much thanks.
  template <size_t N, typename = std::enable_if_t<!std::is_same_v<
                          const char (&)[N], const char_type (&)[N]>>>
  inline bool EqualsLiteral(const char_type (&aStr)[N]) const {
    return *this == nsTLiteralString<char_type>(aStr);
  }

  // The LowerCaseEquals methods compare the ASCII-lowercase version of
  // this string (lowercasing only ASCII uppercase characters) to some
  // ASCII/Literal string. The ASCII string is *not* lowercased for
  // you. If you compare to an ASCII or literal string that contains an
  // uppercase character, it is guaranteed to return false. We will
  // throw assertions too.
  bool NS_FASTCALL LowerCaseEqualsASCII(const char* aData,
                                        size_type aLen) const;
  bool NS_FASTCALL LowerCaseEqualsASCII(const char* aData) const;

  // LowerCaseEqualsLiteral must ONLY be called with an actual literal string,
  // or a char array *constant* declared without an explicit size and with an
  // initializer that is a string literal or is otherwise null-terminated.
  // Use LowerCaseEqualsASCII for other char array variables.
  // (Although this method may happen to produce expected results for other
  // char arrays that have bound one greater than the sequence of interest,
  // such use is discouraged for reasons of readability and maintainability.)
  template <int N>
  bool LowerCaseEqualsLiteral(const char (&aStr)[N]) const {
    return LowerCaseEqualsASCII(aStr, N - 1);
  }

  // Returns true if this string overlaps with the given string fragment.
  bool IsDependentOn(const char_type* aStart, const char_type* aEnd) const {
    // If it _isn't_ the case that one fragment starts after the other ends,
    // or ends before the other starts, then, they conflict:
    //
    //   !(f2.begin >= f1.aEnd || f2.aEnd <= f1.begin)
    //
    // Simplified, that gives us (To avoid relying on Undefined Behavior
    // from comparing pointers from different allocations (which in
    // principle gives the optimizer the permission to assume elsewhere
    // that the pointers are from the same allocation), the comparisons
    // are done on integers, which merely relies on implementation-defined
    // behavior of converting pointers to integers. std::less and
    // std::greater implementations don't actually provide the guarantees
    // that they should.):
    return (reinterpret_cast<uintptr_t>(aStart) <
                reinterpret_cast<uintptr_t>(mData + mLength) &&
            reinterpret_cast<uintptr_t>(aEnd) >
                reinterpret_cast<uintptr_t>(mData));
  }

  bool Contains(const string_view& aString) const {
    return Find(aString) != kNotFound;
  }

  /**
   * Search for the given substring within this string.
   *
   * @param   aString is substring to be sought in this
   * @param   aOffset tells us where in this string to start searching
   * @return  offset in string, or kNotFound
   */
  int32_t Find(const string_view& aString, index_type aOffset = 0) const;

  // Previously there was an overload of `Find()` which took a bool second
  // argument.  Avoid issues by explicitly preventing that overload.
  // TODO: Remove this at some point.
  template <typename I,
            typename = std::enable_if_t<!std::is_same_v<I, index_type> &&
                                        std::is_convertible_v<I, index_type>>>
  int32_t Find(const string_view& aString, I aOffset) const {
    static_assert(!std::is_same_v<I, bool>, "offset must not be `bool`");
    return Find(aString, static_cast<index_type>(aOffset));
  }

  /**
   * Search for the given ASCII substring within this string, ignoring case.
   *
   * @param   aString is substring to be sought in this
   * @param   aOffset tells us where in this string to start searching
   * @return  offset in string, or kNotFound
   */
  int32_t LowerCaseFindASCII(const std::string_view& aString,
                             index_type aOffset = 0) const;

  /**
   * Scan the string backwards, looking for the given substring.
   *
   * @param   aString is substring to be sought in this
   * @return  offset in string, or kNotFound
   */
  int32_t RFind(const string_view& aString) const;

  size_type CountChar(char_type) const;

  bool Contains(char_type aChar) const { return FindChar(aChar) != kNotFound; }

  /**
   * Search for the first instance of a given char within this string
   *
   * @param   aChar is the character to search for
   * @param   aOffset tells us where in this string to start searching
   * @return  offset in string, or kNotFound
   */
  int32_t FindChar(char_type aChar, index_type aOffset = 0) const;

  /**
   * Search for the last instance of a given char within this string
   *
   * @param   aChar is the character to search for
   * @param   aOffset tells us where in this string to start searching
   * @return  offset in string, or kNotFound
   */
  int32_t RFindChar(char_type aChar, int32_t aOffset = -1) const;

  /**
   * This method searches this string for the first character found in
   * the given string.
   *
   * @param aSet contains set of chars to be found
   * @param aOffset tells us where in this string to start searching
   *        (counting from left)
   * @return offset in string, or kNotFound
   */

  int32_t FindCharInSet(const string_view& aSet, index_type aOffset = 0) const;

  /**
   * This method searches this string for the last character found in
   * the given string.
   *
   * @param aSet contains set of chars to be found
   * @param aOffset tells us where in this string to start searching
   *        (counting from left)
   * @return offset in string, or kNotFound
   */

  int32_t RFindCharInSet(const string_view& aSet, int32_t aOffset = -1) const;

  /**
   * Perform locale-independent string to double-precision float conversion.
   *
   * Leading spaces in the string will be ignored. The returned value will be
   * finite unless aErrorCode is set to a failed status.
   *
   * @param   aErrorCode will contain error if one occurs
   * @return  double-precision float rep of string value
   */
  double ToDouble(nsresult* aErrorCode) const;

  /**
   * Perform locale-independent string to single-precision float conversion.
   *
   * Leading spaces in the string will be ignored. The returned value will be
   * finite unless aErrorCode is set to a failed status.
   *
   * @param   aErrorCode will contain error if one occurs
   * @return  single-precision float rep of string value
   */
  float ToFloat(nsresult* aErrorCode) const;

  /**
   * Similar to above ToDouble and ToFloat but allows trailing characters that
   * are not converted.
   */
  double ToDoubleAllowTrailingChars(nsresult* aErrorCode) const;
  float ToFloatAllowTrailingChars(nsresult* aErrorCode) const;

 protected:
  constexpr nsTStringRepr(char_type* aData, size_type aLength,
                          DataFlags aDataFlags, ClassFlags aClassFlags)
      : mData(aData),
        mLength(aLength),
        mDataFlags(aDataFlags),
        mClassFlags(aClassFlags) {}

  static constexpr size_type kMaxCapacity = LengthStorage::kMax;

  /**
   * Checks if the given capacity is valid for this string type.
   */
  [[nodiscard]] static constexpr bool CheckCapacity(size_type aCapacity) {
    return aCapacity <= kMaxCapacity;
  }

  char_type* mData;
  LengthStorage mLength;
  DataFlags mDataFlags;
  ClassFlags const mClassFlags;
};

extern template class nsTStringRepr<char>;
extern template class nsTStringRepr<char16_t>;

}  // namespace detail
}  // namespace mozilla

template <typename T>
int NS_FASTCALL Compare(const mozilla::detail::nsTStringRepr<T>& aLhs,
                        const mozilla::detail::nsTStringRepr<T>& aRhs,
                        nsTStringComparator<T> = nsTDefaultStringComparator<T>);

extern template int NS_FASTCALL Compare<char>(
    const mozilla::detail::nsTStringRepr<char>&,
    const mozilla::detail::nsTStringRepr<char>&, nsTStringComparator<char>);

extern template int NS_FASTCALL
Compare<char16_t>(const mozilla::detail::nsTStringRepr<char16_t>&,
                  const mozilla::detail::nsTStringRepr<char16_t>&,
                  nsTStringComparator<char16_t>);

template <typename T>
inline constexpr bool operator!=(
    const mozilla::detail::nsTStringRepr<T>& aLhs,
    const mozilla::detail::nsTStringRepr<T>& aRhs) {
  return !aLhs.Equals(aRhs);
}

template <typename T>
inline constexpr bool operator!=(const mozilla::detail::nsTStringRepr<T>& aLhs,
                                 const T* aRhs) {
  return !aLhs.Equals(aRhs);
}

template <typename T>
inline bool operator<(const mozilla::detail::nsTStringRepr<T>& aLhs,
                      const mozilla::detail::nsTStringRepr<T>& aRhs) {
  return Compare(aLhs, aRhs) < 0;
}

template <typename T>
inline bool operator<=(const mozilla::detail::nsTStringRepr<T>& aLhs,
                       const mozilla::detail::nsTStringRepr<T>& aRhs) {
  return Compare(aLhs, aRhs) <= 0;
}

template <typename T>
inline bool operator==(const mozilla::detail::nsTStringRepr<T>& aLhs,
                       const mozilla::detail::nsTStringRepr<T>& aRhs) {
  return aLhs.Equals(aRhs);
}

template <typename T>
inline bool operator==(const mozilla::detail::nsTStringRepr<T>& aLhs,
                       const T* aRhs) {
  return aLhs.Equals(aRhs);
}

template <typename T>
inline bool operator>=(const mozilla::detail::nsTStringRepr<T>& aLhs,
                       const mozilla::detail::nsTStringRepr<T>& aRhs) {
  return Compare(aLhs, aRhs) >= 0;
}

template <typename T>
inline bool operator>(const mozilla::detail::nsTStringRepr<T>& aLhs,
                      const mozilla::detail::nsTStringRepr<T>& aRhs) {
  return Compare(aLhs, aRhs) > 0;
}

template <typename Char>
struct fmt::formatter<mozilla::detail::nsTStringRepr<Char>, Char>
    : fmt::formatter<basic_string_view<Char>, Char> {
  template <typename FormatContext>
  constexpr auto format(const mozilla::detail::nsTStringRepr<Char>& aVal,
                        FormatContext& aCtx) const -> decltype(aCtx.out()) {
    return formatter<basic_string_view<Char>, Char>::format(
        basic_string_view<Char>{aVal.BeginReading(), aVal.Length()}, aCtx);
  }
};

#endif
