/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_htmlaccel_htmlaccelNotInline_h
#define mozilla_htmlaccel_htmlaccelNotInline_h

#include "mozilla/Attributes.h"

#include <cstddef>
#include <cstdint>

namespace mozilla::htmlaccel {
// True iff the buffer contains less-than, ampersand, carriage return,
// or U+0000.
//
// This one could probably be inline without LLVM issues when SIMD
// acceleration is statically enabled, but it's probably not worth
// the complexity to do that.
MOZ_NEVER_INLINE bool ContainsMarkup(const char16_t* aPtr,
                                     const char16_t* aEnd);

// HTML Serializer functions

// NOTE! The SkipNonEscaped functions only skip full SIMD strides, but the
// CountEscaped functions also handle the tail that doesn't fit in a SIMD
// stride.

/// Returns the length of the prefix that does not contain less-than,
/// greater-than, ampersand, or no-break space.
/// Examines the input in multiples of 16 code units and does not examine
/// the tail that is left over.
MOZ_NEVER_INLINE std::size_t SkipNonEscapedInTextNode(const char16_t* aPtr,
                                                      const char16_t* aEnd);

/// Returns the length of the prefix that does not contain less-than,
/// greater-than, ampersand, or no-break space.
/// Examines the input in multiples of 16 code units and does not examine
/// the tail that is left over.
MOZ_NEVER_INLINE std::size_t SkipNonEscapedInTextNode(const char* aPtr,
                                                      const char* aEnd);

/// Returns the length of the prefix that does not contain less-than,
/// greater-than, ampersand, no-break space, or double quote.
/// Examines the input in multiples of 16 code units and does not examine
/// the tail that is left over.
MOZ_NEVER_INLINE std::size_t SkipNonEscapedInAttributeValue(
    const char16_t* aPtr, const char16_t* aEnd);

/// Count occurrences of less-than, greater-than, ampersand, and no-break space.
MOZ_NEVER_INLINE uint32_t CountEscapedInTextNode(const char16_t* aPtr,
                                                 const char16_t* aEnd);

/// Count occurrences of less-than, greater-than, ampersand, and no-break space.
MOZ_NEVER_INLINE uint32_t CountEscapedInTextNode(const char* aPtr,
                                                 const char* aEnd);

/// Count occurrences of less-than, greater-than, ampersand, no-break space, and
/// double quote.
MOZ_NEVER_INLINE uint32_t CountEscapedInAttributeValue(const char16_t* aPtr,
                                                       const char16_t* aEnd);

// HTML Tokenizer functions

// Logically these should be MOZ_ALWAYS_INLINE_EVEN_DEBUG if LLVM was working
// as expected. However, these are MOZ_NEVER_INLINE to work around
// https://github.com/llvm/llvm-project/issues/160886 . This way, we get
// a little bit of LICM for the SIMD constants that need to be loaded
// from the constant pool instead of getting materialized by splatting
// an immediate. Once the LLVM bug is fixed, these should be changed
// to MOZ_ALWAYS_INLINE_EVEN_DEBUG to allow the constants to move further
// up to the top of nsHtml5Tokenizer::stateLoop.
//
// The functions for states that accumulate into nsHtml5Tokenizer::strBuf
// take an output pointer and store each examined 16-unit stride there
// before inspecting it. The output therefore receives garbage past the
// returned length, up to the end of the last examined stride, but never
// more code units than there are between aPtr and aEnd. The caller must
// have room for that many code units at aOut, without overlapping the input.

/// The innerHTML / DOMParser case for the data state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateDataFastest(const char16_t* aPtr,
                                               const char16_t* aEnd);

/// View Source case for the data state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateDataViewSource(const char16_t* aPtr,
                                                  const char16_t* aEnd);

/// Normal network case for the data state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateDataLineCol(const char16_t* aPtr,
                                               const char16_t* aEnd);

/// The innerHTML / DOMParser case for the RAWTEXT state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateRawtextFastest(const char16_t* aPtr,
                                                  const char16_t* aEnd);

/// View Source case for the RAWTEXT state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateRawtextViewSource(const char16_t* aPtr,
                                                     const char16_t* aEnd);

/// Normal network case for the RAWTEXT state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateRawtextLineCol(const char16_t* aPtr,
                                                  const char16_t* aEnd);

/// The innerHTML / DOMParser case for the script data escaped state in the
/// HTML parser
MOZ_NEVER_INLINE int32_t
AccelerateScriptDataEscapedFastest(const char16_t* aPtr, const char16_t* aEnd);

/// View Source case for the script data escaped state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateScriptDataEscapedViewSource(
    const char16_t* aPtr, const char16_t* aEnd);

/// Normal network case for the script data escaped state in the HTML parser
MOZ_NEVER_INLINE int32_t
AccelerateScriptDataEscapedLineCol(const char16_t* aPtr, const char16_t* aEnd);

/// The innerHTML / DOMParser case for the comment state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateCommentFastest(const char16_t* aPtr,
                                                  const char16_t* aEnd,
                                                  char16_t* aOut);

/// View Source case for the comment state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateCommentViewSource(const char16_t* aPtr,
                                                     const char16_t* aEnd,
                                                     char16_t* aOut);

/// Normal network case for the comment state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateCommentLineCol(const char16_t* aPtr,
                                                  const char16_t* aEnd,
                                                  char16_t* aOut);

/// The innerHTML / DOMParser case for the attribute value single-quoted state
/// in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateAttributeValueSingleQuotedFastest(
    const char16_t* aPtr, const char16_t* aEnd, char16_t* aOut);

/// View Source case for the attribute value single-quoted state in the HTML
/// parser
MOZ_NEVER_INLINE int32_t AccelerateAttributeValueSingleQuotedViewSource(
    const char16_t* aPtr, const char16_t* aEnd, char16_t* aOut);

/// Normal network case for the attribute value single-quoted state in the HTML
/// parser
MOZ_NEVER_INLINE int32_t AccelerateAttributeValueSingleQuotedLineCol(
    const char16_t* aPtr, const char16_t* aEnd, char16_t* aOut);

/// The innerHTML / DOMParser case for the attribute value double-quoted state
/// in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateAttributeValueDoubleQuotedFastest(
    const char16_t* aPtr, const char16_t* aEnd, char16_t* aOut);

/// View Source case for the attribute value double-quoted state in the HTML
/// parser
MOZ_NEVER_INLINE int32_t AccelerateAttributeValueDoubleQuotedViewSource(
    const char16_t* aPtr, const char16_t* aEnd, char16_t* aOut);

/// Normal network case for the attribute value double-quoted state in the HTML
/// parser
MOZ_NEVER_INLINE int32_t AccelerateAttributeValueDoubleQuotedLineCol(
    const char16_t* aPtr, const char16_t* aEnd, char16_t* aOut);

/// The innerHTML / DOMParser case for the CDATA section state in the HTML
/// parser
MOZ_NEVER_INLINE int32_t AccelerateCdataSectionFastest(const char16_t* aPtr,
                                                       const char16_t* aEnd);

/// View Source case for the CDATA section state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateCdataSectionViewSource(const char16_t* aPtr,
                                                          const char16_t* aEnd);

/// Normal network case for the CDATA section state in the HTML parser
MOZ_NEVER_INLINE int32_t AccelerateCdataSectionLineCol(const char16_t* aPtr,
                                                       const char16_t* aEnd);

/// The innerHTML / DOMParser case for the plaintext state in the HTML parser
MOZ_NEVER_INLINE int32_t AcceleratePlaintextFastest(const char16_t* aPtr,
                                                    const char16_t* aEnd);

/// View Source case for the plaintext state in the HTML parser
MOZ_NEVER_INLINE int32_t AcceleratePlaintextViewSource(const char16_t* aPtr,
                                                       const char16_t* aEnd);

/// Normal network case for the plaintext state in the HTML parser
MOZ_NEVER_INLINE int32_t AcceleratePlaintextLineCol(const char16_t* aPtr,
                                                    const char16_t* aEnd);

}  // namespace mozilla::htmlaccel

#endif  // mozilla_htmlaccel_htmlaccelNotInline_h
