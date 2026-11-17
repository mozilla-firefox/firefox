/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
// IWYU pragma: private, include "nsString.h"

#ifndef nsReadableUtils_h_
#define nsReadableUtils_h_

/**
 * I guess all the routines in this file are all mis-named.
 * According to our conventions, they should be |NS_xxx|.
 */

#include "mozilla/Assertions.h"
#include "mozilla/TextUtils.h"
#include "nsAString.h"
#include "nsTArrayForwardDeclare.h"

// From the nsstring crate
extern "C" {
bool nsstring_fallible_append_utf8_impl(nsAString* aThis, const char* aOther,
                                        size_t aOtherLen, size_t aOldLen);

bool nsstring_fallible_append_latin1_impl(nsAString* aThis, const char* aOther,
                                          size_t aOtherLen, size_t aOldLen,
                                          bool aAllowShrinking);

bool nscstring_fallible_append_utf16_to_utf8_impl(nsACString* aThis,
                                                  const char16_t*,
                                                  size_t aOtherLen,
                                                  size_t aOldLen);

bool nscstring_fallible_append_utf16_to_latin1_lossy_impl(nsACString* aThis,
                                                          const char16_t*,
                                                          size_t aOtherLen,
                                                          size_t aOldLen,
                                                          bool aAllowShrinking);

bool nscstring_fallible_append_utf8_to_latin1_lossy_check(
    nsACString* aThis, const nsACString* aOther, size_t aOldLen);

bool nscstring_fallible_append_latin1_to_utf8_check(nsACString* aThis,
                                                    const nsACString* aOther,
                                                    size_t aOldLen);
}

inline size_t Distance(const nsReadingIterator<char16_t>& aStart,
                       const nsReadingIterator<char16_t>& aEnd) {
  MOZ_ASSERT(aStart.get() <= aEnd.get());
  return static_cast<size_t>(aEnd.get() - aStart.get());
}

inline size_t Distance(const nsReadingIterator<char>& aStart,
                       const nsReadingIterator<char>& aEnd) {
  MOZ_ASSERT(aStart.get() <= aEnd.get());
  return static_cast<size_t>(aEnd.get() - aStart.get());
}

// NOTE: Operations that don't need an operand to be an XPCOM string
// are in mozilla/TextUtils.h and mozilla/Utf8.h.

// UTF-8 to UTF-16
// Invalid UTF-8 byte sequences are replaced with the REPLACEMENT CHARACTER.

[[nodiscard]] inline bool CopyUTF8toUTF16(mozilla::Span<const char> aSource,
                                          nsAString& aDest,
                                          const mozilla::fallible_t&) {
  return nsstring_fallible_append_utf8_impl(&aDest, aSource.Elements(),
                                            aSource.Length(), 0);
}

inline void CopyUTF8toUTF16(mozilla::Span<const char> aSource,
                            nsAString& aDest) {
  if (MOZ_UNLIKELY(!CopyUTF8toUTF16(aSource, aDest, mozilla::fallible))) {
    aDest.AllocFailed(aSource.Length());
  }
}

[[nodiscard]] inline bool AppendUTF8toUTF16(mozilla::Span<const char> aSource,
                                            nsAString& aDest,
                                            const mozilla::fallible_t&) {
  return nsstring_fallible_append_utf8_impl(&aDest, aSource.Elements(),
                                            aSource.Length(), aDest.Length());
}

inline void AppendUTF8toUTF16(mozilla::Span<const char> aSource,
                              nsAString& aDest) {
  if (MOZ_UNLIKELY(!AppendUTF8toUTF16(aSource, aDest, mozilla::fallible))) {
    aDest.AllocFailed(aDest.Length() + aSource.Length());
  }
}

// Latin1 to UTF-16
// Interpret each incoming unsigned byte value as a Unicode scalar value (not
// windows-1252!). The function names say "ASCII" instead of "Latin1" for
// legacy reasons.

[[nodiscard]] inline bool CopyASCIItoUTF16(mozilla::Span<const char> aSource,
                                           nsAString& aDest,
                                           const mozilla::fallible_t&) {
  return nsstring_fallible_append_latin1_impl(&aDest, aSource.Elements(),
                                              aSource.Length(), 0, true);
}

inline void CopyASCIItoUTF16(mozilla::Span<const char> aSource,
                             nsAString& aDest) {
  if (MOZ_UNLIKELY(!CopyASCIItoUTF16(aSource, aDest, mozilla::fallible))) {
    aDest.AllocFailed(aSource.Length());
  }
}

[[nodiscard]] inline bool AppendASCIItoUTF16(mozilla::Span<const char> aSource,
                                             nsAString& aDest,
                                             const mozilla::fallible_t&) {
  return nsstring_fallible_append_latin1_impl(
      &aDest, aSource.Elements(), aSource.Length(), aDest.Length(), false);
}

inline void AppendASCIItoUTF16(mozilla::Span<const char> aSource,
                               nsAString& aDest) {
  if (MOZ_UNLIKELY(!AppendASCIItoUTF16(aSource, aDest, mozilla::fallible))) {
    aDest.AllocFailed(aDest.Length() + aSource.Length());
  }
}

// UTF-16 to UTF-8
// Unpaired surrogates are replaced with the REPLACEMENT CHARACTER.

[[nodiscard]] inline bool CopyUTF16toUTF8(mozilla::Span<const char16_t> aSource,
                                          nsACString& aDest,
                                          const mozilla::fallible_t&) {
  return nscstring_fallible_append_utf16_to_utf8_impl(
      &aDest, aSource.Elements(), aSource.Length(), 0);
}

inline void CopyUTF16toUTF8(mozilla::Span<const char16_t> aSource,
                            nsACString& aDest) {
  if (MOZ_UNLIKELY(!CopyUTF16toUTF8(aSource, aDest, mozilla::fallible))) {
    aDest.AllocFailed(aSource.Length());
  }
}

[[nodiscard]] inline bool AppendUTF16toUTF8(
    mozilla::Span<const char16_t> aSource, nsACString& aDest,
    const mozilla::fallible_t&) {
  return nscstring_fallible_append_utf16_to_utf8_impl(
      &aDest, aSource.Elements(), aSource.Length(), aDest.Length());
}

inline void AppendUTF16toUTF8(mozilla::Span<const char16_t> aSource,
                              nsACString& aDest) {
  if (MOZ_UNLIKELY(!AppendUTF16toUTF8(aSource, aDest, mozilla::fallible))) {
    aDest.AllocFailed(aDest.Length() + aSource.Length());
  }
}

// UTF-16 to Latin1
// If all code points in the input are below U+0100, represents each scalar
// value as an unsigned byte. (This is not windows-1252!) If there are code
// points above U+00FF, memory-safely produces garbage and will likely start
// asserting in future debug builds. The nature of the garbage may differ
// based on CPU architecture and must not be relied upon. The names say
// "ASCII" instead of "Latin1" for legacy reasons.

[[nodiscard]] inline bool LossyCopyUTF16toASCII(
    mozilla::Span<const char16_t> aSource, nsACString& aDest,
    const mozilla::fallible_t&) {
  return nscstring_fallible_append_utf16_to_latin1_lossy_impl(
      &aDest, aSource.Elements(), aSource.Length(), 0, true);
}

inline void LossyCopyUTF16toASCII(mozilla::Span<const char16_t> aSource,
                                  nsACString& aDest) {
  if (MOZ_UNLIKELY(!LossyCopyUTF16toASCII(aSource, aDest, mozilla::fallible))) {
    aDest.AllocFailed(aSource.Length());
  }
}

[[nodiscard]] inline bool LossyAppendUTF16toASCII(
    mozilla::Span<const char16_t> aSource, nsACString& aDest,
    const mozilla::fallible_t&) {
  return nscstring_fallible_append_utf16_to_latin1_lossy_impl(
      &aDest, aSource.Elements(), aSource.Length(), aDest.Length(), false);
}

inline void LossyAppendUTF16toASCII(mozilla::Span<const char16_t> aSource,
                                    nsACString& aDest) {
  if (MOZ_UNLIKELY(
          !LossyAppendUTF16toASCII(aSource, aDest, mozilla::fallible))) {
    aDest.AllocFailed(aDest.Length() + aSource.Length());
  }
}

// Latin1 to UTF-8
// Interpret each incoming unsigned byte value as a Unicode scalar value (not
// windows-1252!).
// If the input is ASCII, the heap-allocated mozilla::StringBuffer is shared if
// possible.

[[nodiscard]] inline bool CopyLatin1toUTF8(const nsACString& aSource,
                                           nsACString& aDest,
                                           const mozilla::fallible_t&) {
  return nscstring_fallible_append_latin1_to_utf8_check(&aDest, &aSource, 0);
}

inline void CopyLatin1toUTF8(const nsACString& aSource, nsACString& aDest) {
  if (MOZ_UNLIKELY(!CopyLatin1toUTF8(aSource, aDest, mozilla::fallible))) {
    aDest.AllocFailed(aSource.Length());
  }
}

[[nodiscard]] inline bool AppendLatin1toUTF8(const nsACString& aSource,
                                             nsACString& aDest,
                                             const mozilla::fallible_t&) {
  return nscstring_fallible_append_latin1_to_utf8_check(&aDest, &aSource,
                                                        aDest.Length());
}

inline void AppendLatin1toUTF8(const nsACString& aSource, nsACString& aDest) {
  if (MOZ_UNLIKELY(!AppendLatin1toUTF8(aSource, aDest, mozilla::fallible))) {
    aDest.AllocFailed(aDest.Length() + aSource.Length());
  }
}

// UTF-8 to Latin1
// If all code points in the input are below U+0100, represents each scalar
// value as an unsigned byte. (This is not windows-1252!) If there are code
// points above U+00FF, memory-safely produces garbage in release builds and
// asserts in debug builds. The nature of the garbage may differ
// based on CPU architecture and must not be relied upon.
// If the input is ASCII, the heap-allocated mozilla::StringBuffer is shared if
// possible.

[[nodiscard]] inline bool LossyCopyUTF8toLatin1(const nsACString& aSource,
                                                nsACString& aDest,
                                                const mozilla::fallible_t&) {
  return nscstring_fallible_append_utf8_to_latin1_lossy_check(&aDest, &aSource,
                                                              0);
}

inline void LossyCopyUTF8toLatin1(const nsACString& aSource,
                                  nsACString& aDest) {
  if (MOZ_UNLIKELY(!LossyCopyUTF8toLatin1(aSource, aDest, mozilla::fallible))) {
    aDest.AllocFailed(aSource.Length());
  }
}

[[nodiscard]] inline bool LossyAppendUTF8toLatin1(const nsACString& aSource,
                                                  nsACString& aDest,
                                                  const mozilla::fallible_t&) {
  return nscstring_fallible_append_utf8_to_latin1_lossy_check(&aDest, &aSource,
                                                              aDest.Length());
}

inline void LossyAppendUTF8toLatin1(const nsACString& aSource,
                                    nsACString& aDest) {
  if (MOZ_UNLIKELY(
          !LossyAppendUTF8toLatin1(aSource, aDest, mozilla::fallible))) {
    aDest.AllocFailed(aDest.Length() + aSource.Length());
  }
}

/**
 * Returns a new |char| buffer containing a zero-terminated copy of |aSource|.
 *
 * Infallibly allocates and returns a new |char| buffer which you must
 * free with |free|.
 * Performs a conversion with LossyConvertUTF16toLatin1() writing into the
 * newly-allocated buffer.
 *
 * The new buffer is zero-terminated, but that may not help you if |aSource|
 * contains embedded nulls.
 *
 * @param aSource a 16-bit wide string
 * @return a new |char| buffer you must free with |free|.
 */
char* ToNewCString(const nsAString& aSource);

/* A fallible version of ToNewCString. Returns nullptr on failure. */
char* ToNewCString(const nsAString& aSource,
                   const mozilla::fallible_t& aFallible);

/**
 * Returns a new |char| buffer containing a zero-terminated copy of |aSource|.
 *
 * Infallibly allocates and returns a new |char| buffer which you must
 * free with |free|.
 *
 * The new buffer is zero-terminated, but that may not help you if |aSource|
 * contains embedded nulls.
 *
 * @param aSource an 8-bit wide string
 * @return a new |char| buffer you must free with |free|.
 */
char* ToNewCString(const nsACString& aSource);

/* A fallible version of ToNewCString. Returns nullptr on failure. */
char* ToNewCString(const nsACString& aSource,
                   const mozilla::fallible_t& aFallible);

/**
 * Returns a new |char| buffer containing a zero-terminated copy of |aSource|.
 *
 * Infallibly allocates and returns a new |char| buffer which you must
 * free with |free|.
 * Performs an encoding conversion from a UTF-16 string to a UTF-8 string with
 * unpaired surrogates replaced with the REPLACEMENT CHARACTER copying
 * |aSource| to your new buffer.
 *
 * The new buffer is zero-terminated, but that may not help you if |aSource|
 * contains embedded nulls.
 *
 * @param aSource a UTF-16 string (made of char16_t's)
 * @param aUTF8Count the number of 8-bit units that was returned
 * @return a new |char| buffer you must free with |free|.
 */
char* ToNewUTF8String(const nsAString& aSource, uint32_t* aUTF8Count = nullptr);

/* A fallible version of ToNewUTF8String. Returns nullptr on failure. */
char* ToNewUTF8String(const nsAString& aSource, uint32_t* aUTF8Count,
                      const mozilla::fallible_t& aFallible);

/**
 * Returns a new |char16_t| buffer containing a zero-terminated copy
 * of |aSource|.
 *
 * Infallibly allocates and returns a new |char16_t| buffer which you must
 * free with |free|.
 *
 * The new buffer is zero-terminated, but that may not help you if |aSource|
 * contains embedded nulls.
 *
 * @param aSource a UTF-16 string
 * @return a new |char16_t| buffer you must free with |free|.
 */
char16_t* ToNewUnicode(const nsAString& aSource);

/* A fallible version of ToNewUnicode. Returns nullptr on failure. */
char16_t* ToNewUnicode(const nsAString& aSource,
                       const mozilla::fallible_t& aFallible);

/**
 * Returns a new |char16_t| buffer containing a zero-terminated copy
 * of |aSource|.
 *
 * Infallibly allocates and returns a new |char16_t| buffer which you must
 * free with|free|.
 *
 * Performs an encoding conversion by 0-padding 8-bit wide characters up to
 * 16-bits wide (i.e. Latin1 to UTF-16 conversion) while copying |aSource|
 * to your new buffer.
 *
 * The new buffer is zero-terminated, but that may not help you if |aSource|
 * contains embedded nulls.
 *
 * @param aSource a Latin1 string
 * @return a new |char16_t| buffer you must free with |free|.
 */
char16_t* ToNewUnicode(const nsACString& aSource);

/* A fallible version of ToNewUnicode. Returns nullptr on failure. */
char16_t* ToNewUnicode(const nsACString& aSource,
                       const mozilla::fallible_t& aFallible);

/**
 * Returns a new |char16_t| buffer containing a zero-terminated copy
 * of |aSource|.
 *
 * Infallibly allocates and returns a new |char| buffer which you must
 * free with |free|.  Performs an encoding conversion from UTF-8 to UTF-16
 * while copying |aSource| to your new buffer.  Malformed byte sequences
 * are replaced with the REPLACEMENT CHARACTER.
 *
 * The new buffer is zero-terminated, but that may not help you if |aSource|
 * contains embedded nulls.
 *
 * @param aSource an 8-bit wide string, UTF-8 encoded
 * @param aUTF16Count the number of 16-bit units that was returned
 * @return a new |char16_t| buffer you must free with |free|.
 *         (UTF-16 encoded)
 */
char16_t* UTF8ToNewUnicode(const nsACString& aSource,
                           uint32_t* aUTF16Count = nullptr);

/* A fallible version of UTF8ToNewUnicode. Returns nullptr on failure. */
char16_t* UTF8ToNewUnicode(const nsACString& aSource, uint32_t* aUTF16Count,
                           const mozilla::fallible_t& aFallible);

/**
 * Copies |aLength| 16-bit code units from the start of |aSource| to the
 * |char16_t| buffer |aDest|.
 *
 * After this operation |aDest| is not null terminated.
 *
 * @param aSource a UTF-16 string
 * @param aSrcOffset start offset in the source string
 * @param aDest a |char16_t| buffer
 * @param aLength the number of 16-bit code units to copy
 * @return pointer to destination buffer - identical to |aDest|
 */
char16_t* CopyUnicodeTo(const nsAString& aSource, uint32_t aSrcOffset,
                        char16_t* aDest, uint32_t aLength);

#if MOZ_HAS_JSRUST()
/**
 * Replaces unpaired surrogates with U+FFFD in the argument.
 *
 * Copies a shared string buffer or an otherwise read-only
 * buffer only if there are unpaired surrogates.
 */
[[nodiscard]] inline bool EnsureUTF16Validity(nsAString& aString) {
  size_t upTo = mozilla::Utf16ValidUpTo(aString);
  size_t len = aString.Length();
  if (upTo == len) {
    return true;
  }
  char16_t* ptr = aString.BeginWriting(mozilla::fallible);
  if (!ptr) {
    return false;
  }
  auto span = mozilla::Span(ptr, len);
  span[upTo] = 0xFFFD;
  mozilla::EnsureUtf16ValiditySpan(span.From(upTo + 1));
  return true;
}
#endif

void ParseString(const nsACString& aSource, char aDelimiter,
                 nsTArray<nsCString>& aArray);

namespace mozilla::detail {

constexpr auto kStringJoinAppendDefault =
    [](auto& aResult, const auto& aValue) { aResult.Append(aValue); };

}  // namespace mozilla::detail

/**
 * Join a sequence of items, each optionally transformed to a string, with a
 * given separator, appending to a given string.
 *
 * \tparam CharType char or char16_t
 * \tparam InputRange a range usable with range-based for
 * \tparam Func optionally, a functor accepting a nsTSubstring<CharType>& and
 *   an item of InputRange which appends the latter to the former
 */
template <
    typename CharType, typename InputRange,
    typename Func = const decltype(mozilla::detail::kStringJoinAppendDefault)&>
void StringJoinAppend(
    nsTSubstring<CharType>& aOutput,
    const nsTLiteralString<CharType>& aSeparator, const InputRange& aInputRange,
    Func&& aFunc = mozilla::detail::kStringJoinAppendDefault) {
  bool first = true;
  for (const auto& item : aInputRange) {
    if (first) {
      first = false;
    } else {
      aOutput.Append(aSeparator);
    }

    aFunc(aOutput, item);
  }
}

/**
 * Join a sequence of items, each optionally transformed to a string, with a
 * given separator, returning a new string.
 *
 * \tparam CharType char or char16_t
 * \tparam InputRange a range usable with range-based for
 * \tparam Func optionally, a functor accepting a nsTSubstring<CharType>& and
 *   an item of InputRange which appends the latter to the former

 */
template <
    typename CharType, typename InputRange,
    typename Func = const decltype(mozilla::detail::kStringJoinAppendDefault)&>
auto StringJoin(const nsTLiteralString<CharType>& aSeparator,
                const InputRange& aInputRange,
                Func&& aFunc = mozilla::detail::kStringJoinAppendDefault) {
  nsTAutoString<CharType> res;
  StringJoinAppend(res, aSeparator, aInputRange, std::forward<Func>(aFunc));
  return res;
}

/**
 * Converts case in place in the argument string.
 */
void ToUpperCase(nsACString&);

void ToLowerCase(nsACString&);

void ToUpperCase(nsACString&);

void ToLowerCase(nsACString&);

/**
 * Converts case from string aSource to aDest.
 */
void ToUpperCase(const nsACString& aSource, nsACString& aDest);

void ToLowerCase(const nsACString& aSource, nsACString& aDest);

/**
 * Finds the leftmost occurrence of |aPattern|, if any in the range
 * |aSearchStart|..|aSearchEnd|.
 *
 * Returns |true| if a match was found, and adjusts |aSearchStart| and
 * |aSearchEnd| to point to the match.  If no match was found, returns |false|
 * and makes |aSearchStart == aSearchEnd|.
 *
 * Currently, this is equivalent to the O(m*n) implementation previously on
 * |ns[C]String|.
 *
 * If we need something faster, then we can implement that later.
 */

bool FindInReadable(const nsAString& aPattern, nsAString::const_iterator&,
                    nsAString::const_iterator&);
bool FindInReadable(const nsACString& aPattern, nsACString::const_iterator&,
                    nsACString::const_iterator&);

/* sometimes we don't care about where the string was, just that we
 * found it or not */
inline bool FindInReadable(const nsAString& aPattern,
                           const nsAString& aSource) {
  return aSource.Contains(aPattern);
}
inline bool FindInReadable(const nsACString& aPattern,
                           const nsACString& aSource) {
  return aSource.Contains(aPattern);
}

bool CaseInsensitiveFindInReadable(const nsACString& aPattern,
                                   nsACString::const_iterator&,
                                   nsACString::const_iterator&);
bool CaseInsensitiveFindInReadable(const nsAString& aPattern,
                                   nsAString::const_iterator&,
                                   nsAString::const_iterator&);
inline bool CaseInsensitiveFindInReadable(const nsACString& aPattern,
                                          const nsACString& aSource) {
  nsACString::const_iterator start, end;
  aSource.BeginReading(start);
  aSource.EndReading(end);
  return CaseInsensitiveFindInReadable(aPattern, start, end);
}
inline bool CaseInsensitiveFindInReadable(const nsAString& aPattern,
                                          const nsAString& aSource) {
  nsAString::const_iterator start, end;
  aSource.BeginReading(start);
  aSource.EndReading(end);
  return CaseInsensitiveFindInReadable(aPattern, start, end);
}

bool AsciiCaseInsensitiveFindInReadable(const nsACString& aPattern,
                                        nsACString::const_iterator&,
                                        nsACString::const_iterator&);
bool AsciiCaseInsensitiveFindInReadable(const nsAString& aPattern,
                                        nsAString::const_iterator&,
                                        nsAString::const_iterator&);
inline bool AsciiCaseInsensitiveFindInReadable(const nsACString& aPattern,
                                               const nsACString& aSource) {
  nsACString::const_iterator start, end;
  aSource.BeginReading(start);
  aSource.EndReading(end);
  return AsciiCaseInsensitiveFindInReadable(aPattern, start, end);
}
inline bool AsciiCaseInsensitiveFindInReadable(const nsAString& aPattern,
                                               const nsAString& aSource) {
  nsAString::const_iterator start, end;
  aSource.BeginReading(start);
  aSource.EndReading(end);
  return AsciiCaseInsensitiveFindInReadable(aPattern, start, end);
}

/**
 * Finds the rightmost occurrence of |aPattern|
 * Returns |true| if a match was found, and adjusts |aSearchStart| and
 * |aSearchEnd| to point to the match.  If no match was found, returns |false|
 * and makes |aSearchStart == aSearchEnd|.
 */
bool RFindInReadable(const nsAString& aPattern, nsAString::const_iterator&,
                     nsAString::const_iterator&);
bool RFindInReadable(const nsACString& aPattern, nsACString::const_iterator&,
                     nsACString::const_iterator&);

bool CaseInsensitiveRFindInReadable(const nsACString& aPattern,
                                    nsACString::const_iterator&,
                                    nsACString::const_iterator&);

/**
 * Finds the leftmost occurrence of |aChar|, if any in the range
 * |aSearchStart|..|aSearchEnd|.
 *
 * Returns |true| if a match was found, and adjusts |aSearchStart| to
 * point to the match.  If no match was found, returns |false| and
 * makes |aSearchStart == aSearchEnd|.
 */
bool FindCharInReadable(char16_t aChar, nsAString::const_iterator& aSearchStart,
                        const nsAString::const_iterator& aSearchEnd);
bool FindCharInReadable(char aChar, nsACString::const_iterator& aSearchStart,
                        const nsACString::const_iterator& aSearchEnd);

bool StringBeginsWith(const nsAString& aSource, const nsAString& aSubstring);
bool StringBeginsWith(const nsAString& aSource, const nsAString& aSubstring,
                      nsStringComparator);
bool StringBeginsWith(const nsACString& aSource, const nsACString& aSubstring);
bool StringBeginsWith(const nsACString& aSource, const nsACString& aSubstring,
                      nsCStringComparator);
bool StringEndsWith(const nsAString& aSource, const nsAString& aSubstring);
bool StringEndsWith(const nsAString& aSource, const nsAString& aSubstring,
                    nsStringComparator);
bool StringEndsWith(const nsACString& aSource, const nsACString& aSubstring);
bool StringEndsWith(const nsACString& aSource, const nsACString& aSubstring,
                    nsCStringComparator);

const nsString& EmptyString();
const nsCString& EmptyCString();

const nsString& VoidString();
const nsCString& VoidCString();

/**
 * Compare a UTF-8 string to an UTF-16 string.
 *
 * Returns 0 if the strings are equal, -1 if aUTF8String is less
 * than aUTF16Count, and 1 in the reverse case. Errors are replaced
 * with U+FFFD and then the U+FFFD is compared as if it had occurred
 * in the input.
 */
int32_t CompareUTF8toUTF16(const nsACString& aUTF8String,
                           const nsAString& aUTF16String);

void AppendUCS4ToUTF16(const uint32_t aSource, nsAString& aDest);

#endif  // !defined(nsReadableUtils_h_)
