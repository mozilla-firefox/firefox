/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/htmlaccel/htmlaccel.h"
#include "mozilla/htmlaccel/htmlaccelNotInline.h"

namespace mozilla::htmlaccel {

// TODO: Perhaps inlining this one on aarch64 wouldn't run into the
// LLVM LICM vs. regalloc bug. But then, inlining this would only
// avoid the overhead of one function call and wouldn't reuse the
// SIMD contants in a useful way.
MOZ_NEVER_INLINE bool ContainsMarkup(const char16_t* aPtr,
                                     const char16_t* aEnd) {
  return detail::ContainsMarkup(aPtr, aEnd);
}

// HTML Serializer functions

/// Skip over SIMD strides not containing less-than, greater-than, ampersand,
/// and no-break space.
MOZ_NEVER_INLINE size_t SkipNonEscapedInTextNode(const char16_t* aPtr,
                                                 const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::LT_GT_AMP_NBSP, true);
}

/// Skip over SIMD strides not containing less-than, greater-than, ampersand,
/// and no-break space.
MOZ_NEVER_INLINE size_t SkipNonEscapedInTextNode(const char* aPtr,
                                                 const char* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::LT_GT_AMP_NBSP, true);
}

/// Skip over SIMD strides not containing less-than, greater-than, ampersand,
/// no-break space, and double quote.
MOZ_NEVER_INLINE size_t SkipNonEscapedInAttributeValue(const char16_t* aPtr,
                                                       const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::LT_GT_AMP_NBSP_QUOT,
                                    true);
}

/// Count occurrences of less-than, greater-than, ampersand, and no-break space.
MOZ_NEVER_INLINE uint32_t CountEscapedInTextNode(const char16_t* aPtr,
                                                 const char16_t* aEnd) {
  return detail::CountEscaped(aPtr, aEnd, false);
}

/// Count occurrences of less-than, greater-than, ampersand, and no-break space.
MOZ_NEVER_INLINE uint32_t CountEscapedInTextNode(const char* aPtr,
                                                 const char* aEnd) {
  return detail::CountEscaped(aPtr, aEnd, false);
}

/// Count occurrences of less-than, greater-than, ampersand, no-break space, and
/// double quote.
MOZ_NEVER_INLINE uint32_t CountEscapedInAttributeValue(const char16_t* aPtr,
                                                       const char16_t* aEnd) {
  return detail::CountEscaped(aPtr, aEnd, true);
}

// HTML Tokenizer functions
//
// The "Fastest" cases don't count line numbers and, therefore, don't need
// to be sensitive to line feeds. "ViewSource" and "LineCol" count line
// numbers and, therefore, care about line feeds.
//
// Even the "Fastest" case needs to care about carriage returns in order
// to be able to normalize CR and CRLF to an LF. (CRLF to LF ends up
// finding the LF without SIMD once the CR has been detected using SIMD.)
//
// The three boolean arguments and their defaults are:
// bool aAllowSurrogates = true,
// bool aAllowHyphen = true,
// bool aAllowRightSquareBracket = true,
//
// Parsing from network (the `LineCol` cases) sets aAllowSurrogates
// to false in order to count column numbers by scalar values instead
// of UTF-16 code unit.
//
// The hyphen and the right square bracket share the low 4 bits (0xD)
// with the carriage return, so they need to be special-cased and can't
// be covered byt the lookup table that's used for other characters
// of interest, since the lookup table already needs to contain CR.

/// The innerHTML / DOMParser case for the data state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateDataFastest(const char16_t* aPtr,
                                               const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::ZERO_LT_AMP_CR, true);
}

/// View Source case for the data state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateDataViewSource(const char16_t* aPtr,
                                                  const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::ZERO_LT_AMP_CR_LF,
                                    true);
}

/// Normal network case for the data state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateDataLineCol(const char16_t* aPtr,
                                               const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::ZERO_LT_AMP_CR_LF,
                                    false);
}

/// The innerHTML / DOMParser case for the RAWTEXT state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateRawtextFastest(const char16_t* aPtr,
                                                  const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::ZERO_LT_CR, true);
}

/// View Source case for the RAWTEXT state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateRawtextViewSource(const char16_t* aPtr,
                                                     const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::ZERO_LT_CR_LF, true);
}

/// Normal network case for the RAWTEXT state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateRawtextLineCol(const char16_t* aPtr,
                                                  const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::ZERO_LT_CR_LF, false);
}

/// The innerHTML / DOMParser case for the script data escaped state in the
/// HTML parser. Shares the characters of interest with the comment state but
/// does not accumulate into strBuf.
MOZ_NEVER_INLINE int32_t
AccelerateScriptDataEscapedFastest(const char16_t* aPtr, const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::ZERO_LT_CR, true,
                                    false);
}

/// View Source case for the script data escaped state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateScriptDataEscapedViewSource(
    const char16_t* aPtr, const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::ZERO_LT_CR_LF, true,
                                    false);
}

/// Normal network case for the script data escaped state in the HTML parser
MOZ_NEVER_INLINE int32_t
AccelerateScriptDataEscapedLineCol(const char16_t* aPtr, const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::ZERO_LT_CR_LF, false,
                                    false);
}

/// The innerHTML / DOMParser case for the comment state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateCommentFastest(const char16_t* aPtr,
                                                  const char16_t* aEnd,
                                                  char16_t* aOut) {
  return detail::AccelerateTextNode(aPtr, aEnd, aOut, detail::ZERO_LT_CR, true,
                                    false);
}

/// View Source case for the comment state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateCommentViewSource(const char16_t* aPtr,
                                                     const char16_t* aEnd,
                                                     char16_t* aOut) {
  return detail::AccelerateTextNode(aPtr, aEnd, aOut, detail::ZERO_LT_CR_LF,
                                    true, false);
}

/// Normal network case for the comment state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateCommentLineCol(const char16_t* aPtr,
                                                  const char16_t* aEnd,
                                                  char16_t* aOut) {
  return detail::AccelerateTextNode(aPtr, aEnd, aOut, detail::ZERO_LT_CR_LF,
                                    false, false);
}

/// The innerHTML / DOMParser case for the attribute value single-quoted state
/// in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateAttributeValueSingleQuotedFastest(
    const char16_t* aPtr, const char16_t* aEnd, char16_t* aOut) {
  return detail::AccelerateTextNode(aPtr, aEnd, aOut, detail::ZERO_APOS_AMP_CR,
                                    true);
}

/// View Source case for the attribute value single-quoted state in the HTML
/// parser
MOZ_NEVER_INLINE int32_t AccelerateAttributeValueSingleQuotedViewSource(
    const char16_t* aPtr, const char16_t* aEnd, char16_t* aOut) {
  return detail::AccelerateTextNode(aPtr, aEnd, aOut,
                                    detail::ZERO_APOS_AMP_CR_LF, true);
}

/// Normal network case for the attribute value single-quoted state in the HTML
/// parser
MOZ_NEVER_INLINE int32_t AccelerateAttributeValueSingleQuotedLineCol(
    const char16_t* aPtr, const char16_t* aEnd, char16_t* aOut) {
  return detail::AccelerateTextNode(aPtr, aEnd, aOut,
                                    detail::ZERO_APOS_AMP_CR_LF, false);
}

/// The innerHTML / DOMParser case for the attribute value double-quoted state
/// in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateAttributeValueDoubleQuotedFastest(
    const char16_t* aPtr, const char16_t* aEnd, char16_t* aOut) {
  return detail::AccelerateTextNode(aPtr, aEnd, aOut, detail::ZERO_QUOT_AMP_CR,
                                    true);
}

/// View Source case for the attribute value double-quoted state in the HTML
/// parser
MOZ_NEVER_INLINE int32_t AccelerateAttributeValueDoubleQuotedViewSource(
    const char16_t* aPtr, const char16_t* aEnd, char16_t* aOut) {
  return detail::AccelerateTextNode(aPtr, aEnd, aOut,
                                    detail::ZERO_QUOT_AMP_CR_LF, true);
}

/// Normal network case for the attribute value double-quoted state in the HTML
/// parser
MOZ_NEVER_INLINE int32_t AccelerateAttributeValueDoubleQuotedLineCol(
    const char16_t* aPtr, const char16_t* aEnd, char16_t* aOut) {
  return detail::AccelerateTextNode(aPtr, aEnd, aOut,
                                    detail::ZERO_QUOT_AMP_CR_LF, false);
}

/// The innerHTML / DOMParser case for the CDATA section state in the HTML
/// parser
MOZ_NEVER_INLINE int32_t AccelerateCdataSectionFastest(const char16_t* aPtr,
                                                       const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::ZERO_CR, true, true,
                                    false);
}

/// View Source case for the CDATA section state in the HTML parser
MOZ_NEVER_INLINE int32_t
AccelerateCdataSectionViewSource(const char16_t* aPtr, const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::ZERO_CR_LF, true, true,
                                    false);
}

/// Normal network case for the CDATA section state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateCdataSectionLineCol(const char16_t* aPtr,
                                                       const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::ZERO_CR_LF, false, true,
                                    false);
}

/// The innerHTML / DOMParser case for the plaintext state in the HTML parser
MOZ_NEVER_INLINE int32_t AcceleratePlaintextFastest(const char16_t* aPtr,
                                                    const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::ZERO_CR, true);
}

/// View Source case for the plaintext state in the HTML parser
MOZ_NEVER_INLINE int32_t AcceleratePlaintextViewSource(const char16_t* aPtr,
                                                       const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::ZERO_CR_LF, true);
}

/// Normal network case for the plaintext state in the HTML parser
MOZ_NEVER_INLINE int32_t AcceleratePlaintextLineCol(const char16_t* aPtr,
                                                    const char16_t* aEnd) {
  return detail::AccelerateTextNode(aPtr, aEnd, detail::ZERO_CR_LF, false);
}

}  // namespace mozilla::htmlaccel
