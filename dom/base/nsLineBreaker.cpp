/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsLineBreaker.h"

#include "gfxTextRun.h"  // for the gfxTextRun::CompressedGlyph::FLAG_BREAK_TYPE_* values
#include "mozilla/AutoRestore.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Utf16.h"
#include "mozilla/intl/LineBreaker.h"  // for LineBreaker::ComputeBreakPositions
#include "mozilla/intl/Locale.h"
#include "mozilla/intl/UnicodeProperties.h"
#include "nsContentUtils.h"
#include "nsHyphenationManager.h"
#include "nsHyphenator.h"

using mozilla::AutoRestore;
using mozilla::Span;
using mozilla::intl::LineBreaker;
using mozilla::intl::LineBreakRule;
using mozilla::intl::Locale;
using mozilla::intl::LocaleParser;
using mozilla::intl::UnicodeProperties;
using mozilla::intl::WordBreakRule;

// There is no break opportunity between any pair of characters that has line
// break class of either AI (Ambiguous), AL (Alphabetic), IS (Infix Numeric
// Separator), NU (Numeric), or QU (Quotation). This is based on UAX #14,
// Unicode Version 15.1.0. See
// https://www.unicode.org/Public/15.1.0/ucd/LineBreak.txt for Unicode code
// point and line break class mapping. If upgrading ICU4X, we have to match
// with Unicode version.
//
// TODO(m_kato):
// We should generate this table by a script (Bug 2081587).
static constexpr uint8_t kNonBreakableLatin1[] = {
    // clang-format off
// 0x20-0x2f
0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 0,
// 0x30-0x3f
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
// 0x40-0x4f
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
// 0x50-0x5f
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1,
// 0x60-0x6f
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
// 0x70-0x7f
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0,
// 0x80-0x8f
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
// 0x90-0x9f
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
// 0xa0-0xaf
0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1,
// 0xb0-0xbf
0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
// 0xc0-0xcf
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
// 0xd0-0xdf
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
// 0xe0-0xef
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
// 0xf0-0xff
1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    // clang-format on
};

template <typename T>
static constexpr bool IsNonBreakableChar(T aChar) {
  if (aChar < 0x20 || aChar > 0xff) {
    return false;
  }
  return !!kNonBreakableLatin1[aChar - 0x20];
}

nsLineBreaker::~nsLineBreaker() {
  NS_ASSERTION(mCurrentWord.Length() == 0,
               "Should have Reset() before destruction!");
}

/* static */
bool nsLineBreaker::ShouldCapitalize(uint32_t aChar, bool& aCapitalizeNext) {
  using mozilla::intl::GeneralCategory;
  auto category = UnicodeProperties::CharType(aChar);
  switch (category) {
    case GeneralCategory::Uppercase_Letter:
    case GeneralCategory::Lowercase_Letter:
    case GeneralCategory::Titlecase_Letter:
    case GeneralCategory::Modifier_Letter:
    case GeneralCategory::Other_Letter:
    case GeneralCategory::Decimal_Number:
    case GeneralCategory::Letter_Number:
    case GeneralCategory::Other_Number:
      if (aCapitalizeNext) {
        aCapitalizeNext = false;
        return true;
      }
      break;
    case GeneralCategory::Space_Separator:
    case GeneralCategory::Line_Separator:
    case GeneralCategory::Paragraph_Separator:
    case GeneralCategory::Dash_Punctuation:
    case GeneralCategory::Initial_Punctuation:
      /* These punctuation categories are excluded, for examples like
       *   "what colo[u]r" -> "What Colo[u]r?" (rather than "What Colo[U]R?")
       * and
       *   "snake_case" -> "Snake_case" (to match word selection behavior)
      case GeneralCategory::Open_Punctuation:
      case GeneralCategory::Close_Punctuation:
      case GeneralCategory::Connector_Punctuation:
       */
      aCapitalizeNext = true;
      break;
    case GeneralCategory::Final_Punctuation:
      /* Special-case: exclude Unicode single-close-quote/apostrophe,
         for examples like "Lowe’s" etc. */
      if (aChar != 0x2019) {
        aCapitalizeNext = true;
      }
      break;
    case GeneralCategory::Other_Punctuation:
      /* Special-case: exclude ASCII apostrophe, for "Lowe's" etc.,
         and MIDDLE DOT, for Catalan "l·l". */
      if (aChar != '\'' && aChar != 0x00B7) {
        aCapitalizeNext = true;
      }
      break;
    default:
      break;
  }
  return false;
}

static void SetupCapitalization(const char16_t* aWord, uint32_t aLength,
                                bool* aCapitalization) {
  // Capitalize the first alphanumeric character after a space or punctuation.
  bool capitalizeNextChar = true;
  for (uint32_t i = 0; i < aLength; ++i) {
    uint32_t ch = aWord[i];
    if (i + 1 < aLength && mozilla::IsSurrogatePair(ch, aWord[i + 1])) {
      ch = mozilla::SurrogateToUCS4(ch, aWord[i + 1]);
    }
    aCapitalization[i] =
        nsLineBreaker::ShouldCapitalize(ch, capitalizeNextChar);

    if (!mozilla::IsInBMP(ch)) {
      ++i;
    }
  }
}

nsresult nsLineBreaker::FlushCurrentWord() {
  auto cleanup = mozilla::MakeScopeExit([&] {
    mCurrentWord.Clear();
    mTextItems.Clear();
    mCurrentWordMightBeBreakable = false;
    mCurrentWordContainsMixedLang = false;
    mCurrentWordLanguage = nullptr;
    mWordContinuation = false;
  });

  uint32_t length = mCurrentWord.Length();
  AutoTArray<uint8_t, 4000> breakState;
  if (!breakState.AppendElements(length, mozilla::fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  if (mLineBreak == LineBreakRule::Anywhere) {
    memset(breakState.Elements(),
           gfxTextRun::CompressedGlyph::FLAG_BREAK_TYPE_NORMAL,
           length * sizeof(uint8_t));
  } else if (!mCurrentWordMightBeBreakable &&
             mWordBreak != WordBreakRule::BreakAll) {
    // word-break: normal or keep-all has no break opportunity if the word
    // is non-breakable. (See the comment of kNonBreakableASCII).
    memset(breakState.Elements(),
           gfxTextRun::CompressedGlyph::FLAG_BREAK_TYPE_NONE,
           length * sizeof(uint8_t));
  } else {
    LineBreaker::ComputeBreakPositions(mCurrentWord, mWordBreak, mLineBreak,
                                       mScriptIsChineseOrJapanese, breakState);
  }

  bool autoHyphenate = mCurrentWordLanguage && !mCurrentWordContainsMixedLang;
  uint32_t i;
  for (i = 0; autoHyphenate && i < mTextItems.Length(); ++i) {
    TextItem* ti = &mTextItems[i];
    if (!(ti->mFlags & BREAK_USE_AUTO_HYPHENATION)) {
      autoHyphenate = false;
    }
  }
  if (autoHyphenate) {
    RefPtr<nsHyphenator> hyphenator =
        nsHyphenationManager::Instance()->GetHyphenator(mCurrentWordLanguage);
    if (hyphenator) {
      FindHyphenationPoints(hyphenator, mCurrentWord.Elements(),
                            mCurrentWord.Elements() + length,
                            breakState.Elements());
    }
  }

  nsTArray<bool> capitalizationState;
  uint32_t offset = 0;
  for (i = 0; i < mTextItems.Length(); ++i) {
    TextItem* ti = &mTextItems[i];
    NS_ASSERTION(ti->mLength > 0, "Zero length word contribution?");

    if ((ti->mFlags & BREAK_SUPPRESS_INITIAL) && ti->mSinkOffset == 0) {
      breakState[offset] = gfxTextRun::CompressedGlyph::FLAG_BREAK_TYPE_NONE;
    }
    if (ti->mFlags & BREAK_SUPPRESS_INSIDE) {
      uint32_t exclude = ti->mSinkOffset == 0 ? 1 : 0;
      memset(breakState.Elements() + offset + exclude,
             gfxTextRun::CompressedGlyph::FLAG_BREAK_TYPE_NONE,
             (ti->mLength - exclude) * sizeof(uint8_t));
    }

    // Don't set the break state for the first character of the word, because
    // it was already set correctly earlier and we don't know what the true
    // value should be.
    uint32_t skipSet = i == 0 ? 1 : 0;
    if (ti->mSink) {
      ti->mSink->SetBreaks(ti->mSinkOffset + skipSet, ti->mLength - skipSet,
                           breakState.Elements() + offset + skipSet);

      if (!mWordContinuation && (ti->mFlags & BREAK_NEED_CAPITALIZATION)) {
        if (capitalizationState.Length() == 0) {
          if (!capitalizationState.AppendElements(length, mozilla::fallible)) {
            return NS_ERROR_OUT_OF_MEMORY;
          }
          memset(capitalizationState.Elements(), false, length * sizeof(bool));
          SetupCapitalization(mCurrentWord.Elements(), length,
                              capitalizationState.Elements());
        }
        ti->mSink->SetCapitalization(ti->mSinkOffset, ti->mLength,
                                     capitalizationState.Elements() + offset);
      }
    }

    offset += ti->mLength;
  }

  return NS_OK;
}

// If the aFlags parameter to AppendText has all these bits set,
// then we don't need to worry about finding break opportunities
// in the appended text.
#define NO_BREAKS_NEEDED_FLAGS                      \
  (BREAK_SUPPRESS_INITIAL | BREAK_SUPPRESS_INSIDE | \
   BREAK_SKIP_SETTING_NO_BREAKS)

nsresult nsLineBreaker::AppendText(nsAtom* aHyphenationLanguage,
                                   const char16_t* aText, uint32_t aLength,
                                   uint32_t aFlags, nsILineBreakSink* aSink) {
  NS_ASSERTION(aLength > 0, "Appending empty text...");

  uint32_t offset = 0;

  // Continue the current word
  if (mCurrentWord.Length() > 0) {
    NS_ASSERTION(!mAfterBreakableSpace && !mBreakHere,
                 "These should not be set");

    while (offset < aLength && !IsSegmentSpace(aText[offset])) {
      if (!mCurrentWordMightBeBreakable &&
          !IsNonBreakableChar<char16_t>(aText[offset])) {
        mCurrentWordMightBeBreakable = true;
      }
      ++offset;
    }

    if (offset > 0) {
      if (!mCurrentWord.AppendElements(aText, offset, mozilla::fallible)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      UpdateCurrentWordLanguage(aHyphenationLanguage);
      mTextItems.AppendElement(TextItem(aSink, 0, offset, aFlags));
    }

    if (offset == aLength) {
      return NS_OK;
    }

    // We encountered whitespace, so we're done with this word
    nsresult rv = FlushCurrentWord();
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  AutoTArray<uint8_t, 4000> breakState;
  if (aSink) {
    if (!breakState.AppendElements(aLength, mozilla::fallible)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  bool noCapitalizationNeeded = true;
  nsTArray<bool> capitalizationState;
  if (aSink && (aFlags & BREAK_NEED_CAPITALIZATION)) {
    if (!capitalizationState.AppendElements(aLength, mozilla::fallible)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    memset(capitalizationState.Elements(), false, aLength * sizeof(bool));
    noCapitalizationNeeded = false;
  }

  uint32_t start = offset;
  bool noBreaksNeeded =
      !aSink || ((aFlags & NO_BREAKS_NEEDED_FLAGS) == NO_BREAKS_NEEDED_FLAGS &&
                 !mBreakHere && !mAfterBreakableSpace);
  if (noBreaksNeeded && noCapitalizationNeeded) {
    // Skip to the space before the last word, since either the break data
    // here is not needed, or no breaks are set in the sink and there cannot
    // be any breaks in this chunk; and we don't need to do word-initial
    // capitalization. All we need is the context for the next chunk (if any).
    offset = aLength;
    while (offset > start) {
      --offset;
      if (IsSegmentSpace(aText[offset])) {
        break;
      }
    }
  }
  uint32_t wordStart = offset;
  bool wordMightBeBreakable = false;

  RefPtr<nsHyphenator> hyphenator;
  if ((aFlags & BREAK_USE_AUTO_HYPHENATION) &&
      !(aFlags & BREAK_SUPPRESS_INSIDE) && aHyphenationLanguage) {
    hyphenator =
        nsHyphenationManager::Instance()->GetHyphenator(aHyphenationLanguage);
  }

  const uint8_t interiorBreakState =
      mWordBreak == WordBreakRule::BreakAll ||
              mLineBreak == LineBreakRule::Anywhere
          ? gfxTextRun::CompressedGlyph::FLAG_BREAK_TYPE_NORMAL
          : gfxTextRun::CompressedGlyph::FLAG_BREAK_TYPE_NONE;

  for (;;) {
    char16_t ch = aText[offset];
    bool isSpace = IsSegmentSpace(ch);
    bool isBreakableSpace = isSpace && !(aFlags & BREAK_SUPPRESS_INSIDE);

    if (aSink && !noBreaksNeeded) {
      breakState[offset] =
          mBreakHere || (mAfterBreakableSpace && !isBreakableSpace) ||
                  mWordBreak == WordBreakRule::BreakAll ||
                  mLineBreak == LineBreakRule::Anywhere
              ? gfxTextRun::CompressedGlyph::FLAG_BREAK_TYPE_NORMAL
              : gfxTextRun::CompressedGlyph::FLAG_BREAK_TYPE_NONE;
    }
    mBreakHere = false;
    mAfterBreakableSpace = isBreakableSpace;

    if (isSpace || ch == '\n') {
      if (offset > wordStart && aSink) {
        if (!(aFlags & BREAK_SUPPRESS_INSIDE)) {
          if (mLineBreak == LineBreakRule::Anywhere) {
            memset(breakState.Elements() + wordStart,
                   gfxTextRun::CompressedGlyph::FLAG_BREAK_TYPE_NORMAL,
                   offset - wordStart);
          } else if (wordMightBeBreakable) {
            // Save current start-of-word state because ComputeBreakPositions()
            // will set it to false.
            AutoRestore<uint8_t> saveWordStartBreakState(breakState[wordStart]);
            LineBreaker::ComputeBreakPositions(
                Span<const char16_t>(aText + wordStart, offset - wordStart),
                mWordBreak, mLineBreak, mScriptIsChineseOrJapanese,
                Span<uint8_t>(breakState.Elements() + wordStart,
                              offset - wordStart));
          }
          if (hyphenator) {
            FindHyphenationPoints(hyphenator, aText + wordStart, aText + offset,
                                  breakState.Elements() + wordStart);
          }
        }
        if (!mWordContinuation && !noCapitalizationNeeded) {
          SetupCapitalization(aText + wordStart, offset - wordStart,
                              capitalizationState.Elements() + wordStart);
        }
      }
      wordMightBeBreakable = false;
      mWordContinuation = false;
      ++offset;
      if (offset >= aLength) {
        break;
      }
      wordStart = offset;
      continue;
    }

    if (!wordMightBeBreakable && !IsNonBreakableChar<char16_t>(ch)) {
      wordMightBeBreakable = true;
    }
    ++offset;
    const uint32_t runStart = offset;
    while (offset < aLength && IsNonBreakableChar<char16_t>(aText[offset])) {
      ++offset;
    }
    if (offset > runStart && !noBreaksNeeded) {
      // Word interiors have no whitespace or boundary state to update.
      memset(breakState.Elements() + runStart, interiorBreakState,
             offset - runStart);
    }
    if (offset >= aLength) {
      // Save this word
      mCurrentWordMightBeBreakable = wordMightBeBreakable;
      uint32_t len = offset - wordStart;
      char16_t* elems = mCurrentWord.AppendElements(len, mozilla::fallible);
      if (!elems) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      memcpy(elems, aText + wordStart, sizeof(char16_t) * len);
      mTextItems.AppendElement(TextItem(aSink, wordStart, len, aFlags));
      // Ensure that the break-before for this word is written out
      offset = wordStart + 1;
      UpdateCurrentWordLanguage(aHyphenationLanguage);
      break;
    }
  }

  if (aSink) {
    if (!noBreaksNeeded) {
      aSink->SetBreaks(start, offset - start, breakState.Elements() + start);
    }
    if (!noCapitalizationNeeded) {
      aSink->SetCapitalization(start, offset - start,
                               capitalizationState.Elements() + start);
    }
  }
  return NS_OK;
}

void nsLineBreaker::FindHyphenationPoints(nsHyphenator* aHyphenator,
                                          const char16_t* aTextStart,
                                          const char16_t* aTextLimit,
                                          uint8_t* aBreakState) {
  // Early-return for words that are definitely too short to hyphenate.
  if (aTextLimit - aTextStart < mHyphenateLimitWord) {
    return;
  }

  nsDependentSubstring string(aTextStart, aTextLimit);
  AutoTArray<bool, 200> hyphens;
  if (NS_FAILED(aHyphenator->Hyphenate(string, hyphens))) {
    return;
  }

  // Keep track of the length seen so far, in terms of characters that are
  // countable for hyphenate-limit-chars purposes.
  uint32_t length = 0;
  // When setting a potential break in aBreakState, we record the previous
  // value in case we need to restore it because the position turns out to
  // be too close to the end of the word.
  struct BreakInfo {
    uint32_t mPosition;
    uint32_t mLength;
    uint8_t mState;
  };
  AutoTArray<BreakInfo, 16> oldBreaks;
  // Don't consider setting any breaks where i >= endLimit, as they will
  // definitely be too near the end of the word to be accepted.
  uint32_t endLimit =
      string.Length() - std::max<uint32_t>(1u, mHyphenateLimitEnd);
  for (uint32_t i = 0; i < string.Length(); ++i) {
    // Get current character, converting surrogate pairs to UCS4 for char
    // category lookup.
    uint32_t ch = string[i];
    if (mozilla::IsHighSurrogate(ch) && i + 1 < string.Length() &&
        mozilla::IsLowSurrogate(string[i + 1])) {
      ch = mozilla::SurrogateToUCS4(ch, string[i + 1]);
    }

    // According to CSS Text, "Nonspacing combining marks (Unicode General
    // Category Mn) and intra-word punctuation (Unicode General Category P*)
    // do not count towards the minimum."
    // (https://drafts.csswg.org/css-text-4/#hyphenate-char-limits)
    // We also don't count Control or Format categories.
    using mozilla::intl::GeneralCategory;
    switch (UnicodeProperties::CharType(ch)) {
      case GeneralCategory::Nonspacing_Mark:
      case GeneralCategory::Dash_Punctuation:
      case GeneralCategory::Open_Punctuation:
      case GeneralCategory::Close_Punctuation:
      case GeneralCategory::Connector_Punctuation:
      case GeneralCategory::Other_Punctuation:
      case GeneralCategory::Initial_Punctuation:
      case GeneralCategory::Final_Punctuation:
      case GeneralCategory::Control:
      case GeneralCategory::Format:
      case GeneralCategory::Surrogate:
        break;
      default:
        ++length;
        break;
    }

    // Don't accept any breaks until we're far enough into the word, or if
    // we're too near the end for it to possibly be accepted. (Note that the
    // check against endLimit is just an initial worst-case check that assumes
    // all the remaining characters are countable; if there are combining
    // marks, etc., in the trailing part of the word we may need to reset the
    // potential break later, after we've fully counted length.)
    if (hyphens[i] && length >= mHyphenateLimitStart && i < endLimit) {
      // Keep track of hyphen position and "countable" length of the word.
      oldBreaks.AppendElement(BreakInfo{i + 1, length, aBreakState[i + 1]});
      aBreakState[i + 1] = gfxTextRun::CompressedGlyph::FLAG_BREAK_TYPE_HYPHEN;
    }

    // If the character was outside the BMP, skip past the low surrogate.
    if (!mozilla::IsInBMP(ch)) {
      ++i;
    }
  }

  if (length < mHyphenateLimitWord) {
    // After discounting combining marks, punctuation, controls, etc., the word
    // was too short for hyphenate-limit-chars. If we've set any hyphen breaks,
    // forget them.
    while (!oldBreaks.IsEmpty()) {
      auto lastBreak = oldBreaks.PopLastElement();
      aBreakState[lastBreak.mPosition] = lastBreak.mState;
    }
  } else {
    // Check if trailing fragment is too short; if so, remove the last hyphen
    // break(s) that we set, until the fragment will be long enough.
    while (!oldBreaks.IsEmpty()) {
      auto lastBreak = oldBreaks.PopLastElement();
      if (length - lastBreak.mLength >= mHyphenateLimitEnd) {
        break;
      }
      aBreakState[lastBreak.mPosition] = lastBreak.mState;
    }
  }
}

nsresult nsLineBreaker::AppendText(nsAtom* aHyphenationLanguage,
                                   const uint8_t* aText, uint32_t aLength,
                                   uint32_t aFlags, nsILineBreakSink* aSink) {
  NS_ASSERTION(aLength > 0, "Appending empty text...");

  if (aFlags & (BREAK_NEED_CAPITALIZATION | BREAK_USE_AUTO_HYPHENATION)) {
    // Defer to the Unicode path if capitalization or hyphenation is required
    nsAutoString str;
    const char* cp = reinterpret_cast<const char*>(aText);
    CopyASCIItoUTF16(nsDependentCSubstring(cp, cp + aLength), str);
    return AppendText(aHyphenationLanguage, str.get(), aLength, aFlags, aSink);
  }

  uint32_t offset = 0;

  // Continue the current word
  if (mCurrentWord.Length() > 0) {
    NS_ASSERTION(!mAfterBreakableSpace && !mBreakHere,
                 "These should not be set");

    while (offset < aLength && !IsSegmentSpace(aText[offset])) {
      if (!mCurrentWordMightBeBreakable &&
          !IsNonBreakableChar<uint8_t>(aText[offset])) {
        mCurrentWordMightBeBreakable = true;
      }
      ++offset;
    }

    if (offset > 0) {
      if (!mCurrentWord.AppendElements(aText, offset, mozilla::fallible)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      mTextItems.AppendElement(TextItem(aSink, 0, offset, aFlags));
    }

    if (offset == aLength) {
      // We did not encounter whitespace so the word hasn't finished yet.
      return NS_OK;
    }

    // We encountered whitespace, so we're done with this word
    nsresult rv = FlushCurrentWord();
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  AutoTArray<uint8_t, 4000> breakState;
  if (aSink) {
    if (!breakState.AppendElements(aLength, mozilla::fallible)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  uint32_t start = offset;
  bool noBreaksNeeded =
      !aSink || ((aFlags & NO_BREAKS_NEEDED_FLAGS) == NO_BREAKS_NEEDED_FLAGS &&
                 !mBreakHere && !mAfterBreakableSpace);
  if (noBreaksNeeded) {
    // Skip to the space before the last word, since either the break data
    // here is not needed, or no breaks are set in the sink and there cannot
    // be any breaks in this chunk; all we need is the context for the next
    // chunk (if any)
    offset = aLength;
    while (offset > start) {
      --offset;
      if (IsSegmentSpace(aText[offset])) {
        break;
      }
    }
  }
  uint32_t wordStart = offset;
  bool wordMightBeBreakable = false;

  const uint8_t interiorBreakState =
      mWordBreak == WordBreakRule::BreakAll ||
              mLineBreak == LineBreakRule::Anywhere
          ? gfxTextRun::CompressedGlyph::FLAG_BREAK_TYPE_NORMAL
          : gfxTextRun::CompressedGlyph::FLAG_BREAK_TYPE_NONE;

  for (;;) {
    uint8_t ch = aText[offset];
    bool isSpace = IsSegmentSpace(ch);
    bool isBreakableSpace = isSpace && !(aFlags & BREAK_SUPPRESS_INSIDE);

    if (aSink) {
      // Consider word-break style.  Since the break position of CJK scripts
      // will be set by nsILineBreaker, we don't consider CJK at this point.
      breakState[offset] =
          mBreakHere || (mAfterBreakableSpace && !isBreakableSpace) ||
                  mWordBreak == WordBreakRule::BreakAll ||
                  mLineBreak == LineBreakRule::Anywhere
              ? gfxTextRun::CompressedGlyph::FLAG_BREAK_TYPE_NORMAL
              : gfxTextRun::CompressedGlyph::FLAG_BREAK_TYPE_NONE;
    }
    mBreakHere = false;
    mAfterBreakableSpace = isBreakableSpace;

    if (isSpace) {
      if (offset > wordStart && aSink && !(aFlags & BREAK_SUPPRESS_INSIDE)) {
        if (mLineBreak == LineBreakRule::Anywhere) {
          memset(breakState.Elements() + wordStart,
                 gfxTextRun::CompressedGlyph::FLAG_BREAK_TYPE_NORMAL,
                 offset - wordStart);
        } else if (wordMightBeBreakable) {
          // Save current start-of-word state because ComputeBreakPositions()
          // will set it to false.
          AutoRestore<uint8_t> saveWordStartBreakState(breakState[wordStart]);
          LineBreaker::ComputeBreakPositions(
              Span<const uint8_t>(aText + wordStart, offset - wordStart),
              mWordBreak, mLineBreak, mScriptIsChineseOrJapanese,
              Span<uint8_t>(breakState.Elements() + wordStart,
                            offset - wordStart));
        }
      }

      wordMightBeBreakable = false;
      mWordContinuation = false;
      ++offset;
      if (offset >= aLength) {
        break;
      }
      wordStart = offset;
      continue;
    }

    if (!wordMightBeBreakable && !IsNonBreakableChar<uint8_t>(ch)) {
      wordMightBeBreakable = true;
    }
    ++offset;
    const uint32_t runStart = offset;
    while (offset < aLength && IsNonBreakableChar<uint8_t>(aText[offset])) {
      ++offset;
    }
    if (offset > runStart && aSink) {
      memset(breakState.Elements() + runStart, interiorBreakState,
             offset - runStart);
    }
    if (offset >= aLength) {
      // Save this word
      mCurrentWordMightBeBreakable = wordMightBeBreakable;
      uint32_t len = offset - wordStart;
      char16_t* elems = mCurrentWord.AppendElements(len, mozilla::fallible);
      if (!elems) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      uint32_t i;
      for (i = wordStart; i < offset; ++i) {
        elems[i - wordStart] = aText[i];
      }
      mTextItems.AppendElement(TextItem(aSink, wordStart, len, aFlags));
      // Ensure that the break-before for this word is written out
      offset = wordStart + 1;
      break;
    }
  }

  if (!noBreaksNeeded) {
    aSink->SetBreaks(start, offset - start, breakState.Elements() + start);
  }
  return NS_OK;
}

void nsLineBreaker::UpdateCurrentWordLanguage(nsAtom* aHyphenationLanguage) {
  if (mCurrentWordLanguage && mCurrentWordLanguage != aHyphenationLanguage) {
    mCurrentWordContainsMixedLang = true;
    mScriptIsChineseOrJapanese = false;
    return;
  }

  if (aHyphenationLanguage && !mCurrentWordLanguage) {
    static mozilla::StaticRefPtr<nsAtom> sLastHyphenationLanguage;
    static bool sLastScriptIsChineseOrJapanese = false;
    static bool sInit = false;

    if (!sInit) {
      mozilla::ClearOnShutdown(&sLastHyphenationLanguage);
      sInit = true;
    }

    if (sLastHyphenationLanguage == aHyphenationLanguage) {
      MOZ_ASSERT(nsAtomString(sLastHyphenationLanguage)
                     .Equals(nsAtomString(aHyphenationLanguage)));
      mScriptIsChineseOrJapanese = sLastScriptIsChineseOrJapanese;
    } else {
      Locale loc;
      auto result =
          LocaleParser::TryParse(nsAtomCString(aHyphenationLanguage), loc);

      if (result.isErr()) {
        return;
      }
      if (loc.Script().Missing() && loc.AddLikelySubtags().isErr()) {
        return;
      }
      mScriptIsChineseOrJapanese =
          loc.Script().EqualTo("Hans") || loc.Script().EqualTo("Hant") ||
          loc.Script().EqualTo("Jpan") || loc.Script().EqualTo("Hrkt");

      sLastHyphenationLanguage = aHyphenationLanguage;
      sLastScriptIsChineseOrJapanese = mScriptIsChineseOrJapanese;
    }
  }
  mCurrentWordLanguage = aHyphenationLanguage;
}

nsresult nsLineBreaker::AppendInvisibleWhitespace(uint32_t aFlags) {
  nsresult rv = FlushCurrentWord();
  if (NS_FAILED(rv)) {
    return rv;
  }

  bool isBreakableSpace = !(aFlags & BREAK_SUPPRESS_INSIDE);
  if (mAfterBreakableSpace && !isBreakableSpace) {
    mBreakHere = true;
  }
  mAfterBreakableSpace = isBreakableSpace;
  mWordContinuation = false;
  return NS_OK;
}

nsresult nsLineBreaker::Reset(bool* aTrailingBreak) {
  nsresult rv = FlushCurrentWord();
  if (NS_FAILED(rv)) {
    return rv;
  }

  *aTrailingBreak = mBreakHere || mAfterBreakableSpace;
  mBreakHere = false;
  mAfterBreakableSpace = false;
  return NS_OK;
}
