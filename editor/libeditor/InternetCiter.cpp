/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "InternetCiter.h"

#include "mozilla/IntegerRange.h"
#include "mozilla/intl/Segmenter.h"
#include "HTMLEditUtils.h"
#include "nsAString.h"
#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsDebug.h"
#include "nsDependentSubstring.h"
#include "nsError.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsStringIterator.h"

#include <algorithm>

namespace mozilla {

/**
 * Mail citations using the Internet style: > This is a citation.
 */

void InternetCiter::GetCiteString(const nsAString& aInString,
                                  nsAString& aOutString) {
  aOutString.Truncate();
  char16_t uch = HTMLEditUtils::kNewLine;

  // Strip trailing new lines which will otherwise turn up
  // as ugly quoted empty lines.
  nsReadingIterator<char16_t> beginIter, endIter;
  aInString.BeginReading(beginIter);
  aInString.EndReading(endIter);
  while (beginIter != endIter && (*endIter == HTMLEditUtils::kCarriageReturn ||
                                  *endIter == HTMLEditUtils::kNewLine)) {
    --endIter;
  }

  while (beginIter != endIter) {
    if (uch == HTMLEditUtils::kNewLine) {
      aOutString.Append(HTMLEditUtils::kGreaterThan);
      // Do not insert a space between adjacent > quote markers.  This keeps
      // ">>> " style quoting for RFC 2646 format=flowed compatibility.
      if (*beginIter != HTMLEditUtils::kGreaterThan) {
        aOutString.Append(HTMLEditUtils::kSpace);
      }
    }

    uch = *beginIter;
    ++beginIter;

    aOutString += uch;
  }

  if (uch != HTMLEditUtils::kNewLine) {
    aOutString += HTMLEditUtils::kNewLine;
  }
}

uint32_t InternetCiter::QuotePrefixLength(uint32_t aQuoteLevel) {
  return aQuoteLevel ? aQuoteLevel + 1 : 0;
}

void InternetCiter::AppendQuoteMarkers(nsAString& aOutString,
                                       uint32_t aQuoteLevel) {
  DebugOnly<uint32_t> oldLength = aOutString.Length();
  for ([[maybe_unused]] uint32_t i : IntegerRange(aQuoteLevel)) {
    aOutString.Append(HTMLEditUtils::kGreaterThan);
  }
  if (aQuoteLevel > 0) {
    aOutString.Append(HTMLEditUtils::kSpace);
  }
  MOZ_ASSERT(aOutString.Length() - oldLength == QuotePrefixLength(aQuoteLevel));
}

bool InternetCiter::IsSpace(char16_t aChar) {
  return nsCRT::IsAsciiSpace(aChar) || aChar == HTMLEditUtils::kNBSP;
}

bool InternetCiter::IsBodyBoundarySpace(char16_t aChar) {
  return nsCRT::IsAsciiSpace(aChar) && aChar != HTMLEditUtils::kNewLine &&
         aChar != HTMLEditUtils::kCarriageReturn;
}

struct InternetCiter::BodyRange {
  uint32_t mStart = 0;
  uint32_t mEnd = 0;

  bool IsEmpty() const { return mStart == mEnd; }
  uint32_t Length() const { return mEnd - mStart; }
};

struct InternetCiter::PhysicalLine {
  uint32_t mQuoteLevel = 0;
  uint32_t mBodyStart = 0;
  uint32_t mBodyEnd = 0;
  bool mEndedWithNewLine = false;

  bool IsQuoted() const { return mQuoteLevel > 0; }
  uint32_t NextLineStart() const {
    return mBodyEnd + (mEndedWithNewLine ? 1 : 0);
  }
  BodyRange GetTrimmedBody(const nsPromiseFlatString& aString) const;

  static PhysicalLine FromString(const nsPromiseFlatString& aString,
                                 uint32_t aLineStart);
};

InternetCiter::PhysicalLine InternetCiter::PhysicalLine::FromString(
    const nsPromiseFlatString& aString, uint32_t aLineStart) {
  const uint32_t length = aString.Length();
  PhysicalLine line;
  uint32_t offset = aLineStart;

  while (offset < length && aString[offset] == HTMLEditUtils::kGreaterThan) {
    ++line.mQuoteLevel;
    ++offset;
    while (offset < length && aString[offset] == HTMLEditUtils::kSpace) {
      ++offset;
    }
  }

  line.mBodyStart = offset;
  while (offset < length && aString[offset] != HTMLEditUtils::kNewLine) {
    ++offset;
  }
  line.mBodyEnd = offset;
  line.mEndedWithNewLine = offset < length;
  return line;
}

InternetCiter::BodyRange InternetCiter::PhysicalLine::GetTrimmedBody(
    const nsPromiseFlatString& aString) const {
  BodyRange range{mBodyStart, mBodyEnd};
  while (range.mStart < range.mEnd &&
         InternetCiter::IsBodyBoundarySpace(aString[range.mStart])) {
    ++range.mStart;
  }
  while (range.mEnd > range.mStart &&
         InternetCiter::IsBodyBoundarySpace(aString[range.mEnd - 1])) {
    --range.mEnd;
  }
  return range;
}

void InternetCiter::EnsureEndsWithLineBreak(nsAString& aOutString) {
  if (!aOutString.IsEmpty() && aOutString.Last() != HTMLEditUtils::kNewLine) {
    aOutString.Append(HTMLEditUtils::kNewLine);
  }
}

void InternetCiter::StartOutputLine(nsAString& aOutString,
                                    uint32_t aQuoteLevel) {
  EnsureEndsWithLineBreak(aOutString);
  AppendQuoteMarkers(aOutString, aQuoteLevel);
}

struct InternetCiter::QuotedParagraph {
  uint32_t mQuoteLevel = 0;
  uint32_t mBodyColumn = 0;
  bool mHasOpenLine = false;

  void StartLine(nsAString& aOutString, uint32_t aQuoteLevel);
  void FinishLine(nsAString& aOutString);
  void Reset();
  void AppendBodySeparator(nsAString& aOutString, uint32_t aBodyWrapColumn);
  void AppendBodyRange(nsAString& aOutString,
                       const nsPromiseFlatString& aString, uint32_t aQuoteLevel,
                       const BodyRange& aRange, uint32_t aWrapCol);
};

void InternetCiter::QuotedParagraph::StartLine(nsAString& aOutString,
                                               uint32_t aQuoteLevel) {
  if (mHasOpenLine && mQuoteLevel == aQuoteLevel) {
    return;
  }
  InternetCiter::StartOutputLine(aOutString, aQuoteLevel);
  mQuoteLevel = aQuoteLevel;
  mBodyColumn = 0;
  mHasOpenLine = true;
}

void InternetCiter::QuotedParagraph::FinishLine(nsAString& aOutString) {
  if (mHasOpenLine) {
    InternetCiter::EnsureEndsWithLineBreak(aOutString);
    mBodyColumn = 0;
    mHasOpenLine = false;
  }
}

void InternetCiter::QuotedParagraph::Reset() {
  mQuoteLevel = 0;
  mBodyColumn = 0;
  mHasOpenLine = false;
}

uint32_t InternetCiter::ShortestUsefulQuotedBodyColumn(uint32_t aWrapCol) {
  // Require each quoted line to carry at least this many body columns.  With
  // the default 72-column wrap, that minimum is 24 columns.
  constexpr uint32_t kShortestUsefulQuotedBodyColumn = 16;
  constexpr uint32_t kShortestUsefulBodyFractionDenominator = 3;
  return std::max(kShortestUsefulQuotedBodyColumn,
                  aWrapCol / kShortestUsefulBodyFractionDenominator);
}

uint32_t InternetCiter::BodyWrapColumn(uint32_t aQuoteLevel,
                                       uint32_t aWrapCol) {
  const uint32_t quotePrefixLength = QuotePrefixLength(aQuoteLevel);
  const uint32_t bodyColumnsWithinWrapColumn =
      quotePrefixLength < aWrapCol ? aWrapCol - quotePrefixLength : 0;
  // Do not wrap quoted bodies narrower than the minimum useful body column.
  return std::max(bodyColumnsWithinWrapColumn,
                  ShortestUsefulQuotedBodyColumn(aWrapCol));
}

uint32_t InternetCiter::FindBodyLineBreak(const nsPromiseFlatString& aString,
                                          const BodyRange& aRange,
                                          uint32_t aBodyWrapColumn) {
  MOZ_ASSERT(!aRange.IsEmpty());
  MOZ_ASSERT(aBodyWrapColumn);

  uint32_t breakLength = 0;
  Maybe<uint32_t> nextBreakLength;
  intl::LineBreakIteratorUtf16 lineBreakIter(
      Span<const char16_t>(aString.get() + aRange.mStart, aRange.Length()));
  while (true) {
    nextBreakLength = lineBreakIter.Next();
    if (!nextBreakLength || *nextBreakLength > aBodyWrapColumn) {
      break;
    }
    breakLength = *nextBreakLength;
  }

  if (breakLength) {
    return breakLength;
  }

  if (nextBreakLength && *nextBreakLength) {
    return *nextBreakLength;
  }

  // Fallback to always return a positive length.
  return std::min(aRange.Length(), std::max(1u, aBodyWrapColumn));
}

uint32_t InternetCiter::AppendBodySlice(nsAString& aOutString,
                                        const nsPromiseFlatString& aString,
                                        uint32_t aBodyStart, uint32_t aLength) {
  while (aLength && IsSpace(aString[aBodyStart + aLength - 1])) {
    --aLength;
  }
  aOutString.Append(Substring(aString, aBodyStart, aLength));
  return aLength;
}

void InternetCiter::QuotedParagraph::AppendBodySeparator(
    nsAString& aOutString, uint32_t aBodyWrapColumn) {
  if (!mHasOpenLine || !mBodyColumn) {
    return;
  }

  if (mBodyColumn + 1 > aBodyWrapColumn) {
    FinishLine(aOutString);
    return;
  }

  aOutString.Append(HTMLEditUtils::kSpace);
  ++mBodyColumn;
}

void InternetCiter::QuotedParagraph::AppendBodyRange(
    nsAString& aOutString, const nsPromiseFlatString& aString,
    uint32_t aQuoteLevel, const BodyRange& aRange, uint32_t aWrapCol) {
  if (aRange.IsEmpty()) {
    return;
  }
  BodyRange range = aRange;

  const uint32_t bodyWrapColumn =
      InternetCiter::BodyWrapColumn(aQuoteLevel, aWrapCol);
  AppendBodySeparator(aOutString, bodyWrapColumn);

  while (!range.IsEmpty()) {
    StartLine(aOutString, aQuoteLevel);

    const uint32_t availableBodyColumns =
        bodyWrapColumn > mBodyColumn ? bodyWrapColumn - mBodyColumn : 0;
    if (!availableBodyColumns) {
      FinishLine(aOutString);
      continue;
    }

    if (range.Length() <= availableBodyColumns) {
      aOutString.Append(Substring(aString, range.mStart, range.Length()));
      mBodyColumn += range.Length();
      return;
    }

    const uint32_t breakLength =
        InternetCiter::FindBodyLineBreak(aString, range, availableBodyColumns);
    constexpr uint32_t kLongWordOverflowTolerance = 6;
    // Allow words to slightly exceed the break limit to keep lines readable.
    if (breakLength > availableBodyColumns && mBodyColumn &&
        breakLength - availableBodyColumns > kLongWordOverflowTolerance) {
      // Do not split URLs at the beginning of lines, so they remain clickable.
      FinishLine(aOutString);
      continue;
    }

    mBodyColumn += InternetCiter::AppendBodySlice(aOutString, aString,
                                                  range.mStart, breakLength);
    range.mStart += breakLength;
    while (range.mStart < range.mEnd &&
           InternetCiter::IsSpace(aString[range.mStart])) {
      ++range.mStart;
    }
    if (!range.IsEmpty()) {
      FinishLine(aOutString);
    }
  }
}

void InternetCiter::EmitBlankQuotedLine(nsAString& aOutString,
                                        uint32_t aQuoteLevel,
                                        bool aEndedWithNewLine,
                                        QuotedParagraph& aParagraph) {
  aParagraph.FinishLine(aOutString);
  StartOutputLine(aOutString, aQuoteLevel);
  if (aEndedWithNewLine) {
    aOutString.Append(HTMLEditUtils::kNewLine);
  }
  aParagraph.Reset();
}

void InternetCiter::EmitUnquotedLine(nsAString& aOutString,
                                     const nsPromiseFlatString& aString,
                                     const PhysicalLine& aLine,
                                     QuotedParagraph& aParagraph) {
  aParagraph.FinishLine(aOutString);
  aParagraph.Reset();
  StartOutputLine(aOutString, 0);
  aOutString.Append(
      Substring(aString, aLine.mBodyStart, aLine.mBodyEnd - aLine.mBodyStart));
  if (aLine.mEndedWithNewLine) {
    aOutString.Append(HTMLEditUtils::kNewLine);
  }
}

void InternetCiter::Rewrap(const nsAString& aInString, uint32_t aWrapCol,
                           uint32_t /*aFirstLineOffset*/,
                           bool /*aRespectNewlines*/, nsAString& aOutString) {
  // Rewrap operates on DOM newlines.
#ifdef DEBUG
  int32_t crPosition = aInString.FindChar(HTMLEditUtils::kCarriageReturn);
  NS_ASSERTION(crPosition < 0, "Rewrap: CR in string gotten from DOM!\n");
#endif /* DEBUG */

  aOutString.Truncate();

  const nsPromiseFlatString& tString = PromiseFlatString(aInString);
  const uint32_t length = tString.Length();
  QuotedParagraph quotedParagraph;

  // Rewrap quoted mail as a sequence of physical lines:
  //   1. Parse the quote prefix and body range.
  //   2. Join adjacent quoted body lines with the same quote level.
  //   3. Wrap only body text, then re-emit the quote prefix.
  for (uint32_t nextLineStart = 0; nextLineStart < length;) {
    const PhysicalLine line = PhysicalLine::FromString(tString, nextLineStart);
    nextLineStart = line.NextLineStart();

    if (!line.IsQuoted()) {
      EmitUnquotedLine(aOutString, tString, line, quotedParagraph);
      continue;
    }

    if (quotedParagraph.mHasOpenLine &&
        quotedParagraph.mQuoteLevel != line.mQuoteLevel) {
      quotedParagraph.FinishLine(aOutString);
    }

    const BodyRange body = line.GetTrimmedBody(tString);
    if (body.IsEmpty()) {
      EmitBlankQuotedLine(aOutString, line.mQuoteLevel, line.mEndedWithNewLine,
                          quotedParagraph);
      continue;
    }

    quotedParagraph.AppendBodyRange(aOutString, tString, line.mQuoteLevel, body,
                                    aWrapCol);
  }
}

}  // namespace mozilla
