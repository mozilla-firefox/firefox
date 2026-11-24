/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxSkipChars.h"

#include "mozilla/BinarySearch.h"
#include "mozilla/gfx/Logging.h"

struct SkippedRangeStartComparator {
  const uint32_t mOffset;
  explicit SkippedRangeStartComparator(const uint32_t aOffset)
      : mOffset(aOffset) {}
  int operator()(const gfxSkipChars::SkippedRange& aRange) const {
    return (mOffset < aRange.Start()) ? -1 : 1;
  }
};

void gfxSkipCharsIterator::SetOriginalOffset(int32_t aOffset) {
  aOffset += mOriginalStringToSkipCharsOffset;
  if (MOZ_UNLIKELY(uint32_t(aOffset) > mSkipChars->mCharCount)) {
    gfxCriticalError() << "invalid offset " << aOffset
                       << " for gfxSkipChars length " << mSkipChars->mCharCount;
    aOffset = mSkipChars->mCharCount;
  }

  mOriginalStringOffset = aOffset;

  const uint32_t rangeCount = mSkipChars->mRanges.Length();
  if (rangeCount == 0) {
    mSkippedStringOffset = aOffset;
    return;
  }

  // at start of string?
  if (aOffset == 0) {
    mSkippedStringOffset = 0;
    mCurrentRangeIndex =
        rangeCount && mSkipChars->mRanges[0].Start() == 0 ? 0 : -1;
    return;
  }

  // find the range that includes or precedes aOffset
  const nsTArray<gfxSkipChars::SkippedRange>& ranges = mSkipChars->mRanges;
  size_t idx;
  mozilla::BinarySearchIf(ranges, 0, rangeCount,
                          SkippedRangeStartComparator(aOffset), &idx);

  if (idx == rangeCount) {
    mCurrentRangeIndex = rangeCount - 1;
  } else if (uint32_t(aOffset) < ranges[idx].Start()) {
    mCurrentRangeIndex = idx - 1;
    if (mCurrentRangeIndex == -1) {
      mSkippedStringOffset = aOffset;
      return;
    }
  } else {
    mCurrentRangeIndex = idx;
  }

  const gfxSkipChars::SkippedRange& r = ranges[mCurrentRangeIndex];
  if (uint32_t(aOffset) < r.End()) {
    mSkippedStringOffset = r.SkippedOffset();
    return;
  }

  mSkippedStringOffset = aOffset - r.NextDelta();
}

struct SkippedRangeOffsetComparator {
  const uint32_t mOffset;
  explicit SkippedRangeOffsetComparator(const uint32_t aOffset)
      : mOffset(aOffset) {}
  int operator()(const gfxSkipChars::SkippedRange& aRange) const {
    return (mOffset < aRange.SkippedOffset()) ? -1 : 1;
  }
};

void gfxSkipCharsIterator::SetSkippedOffset(uint32_t aOffset) {
  NS_ASSERTION(
      (mSkipChars->mRanges.IsEmpty() && aOffset <= mSkipChars->mCharCount) ||
          (aOffset <= mSkipChars->LastRange().SkippedOffset() +
                          mSkipChars->mCharCount -
                          mSkipChars->LastRange().End()),
      "Invalid skipped offset");
  mSkippedStringOffset = aOffset;

  uint32_t rangeCount = mSkipChars->mRanges.Length();
  if (rangeCount == 0) {
    mOriginalStringOffset = aOffset;
    return;
  }

  const nsTArray<gfxSkipChars::SkippedRange>& ranges = mSkipChars->mRanges;
  size_t idx;
  mozilla::BinarySearchIf(ranges, 0, rangeCount,
                          SkippedRangeOffsetComparator(aOffset), &idx);

  if (idx == rangeCount) {
    mCurrentRangeIndex = rangeCount - 1;
  } else if (aOffset < ranges[idx].SkippedOffset()) {
    mCurrentRangeIndex = idx - 1;
    if (mCurrentRangeIndex == -1) {
      mOriginalStringOffset = aOffset;
      return;
    }
  } else {
    mCurrentRangeIndex = idx;
  }

  const gfxSkipChars::SkippedRange& r = ranges[mCurrentRangeIndex];
  mOriginalStringOffset = r.End() + aOffset - r.SkippedOffset();
}

bool gfxSkipCharsIterator::IsOriginalCharSkipped(int32_t* aRunLength) const {
  if (mCurrentRangeIndex == -1) {
    // we're before the first skipped range (if any)
    if (aRunLength) {
      uint32_t end = mSkipChars->mRanges.IsEmpty()
                         ? mSkipChars->mCharCount
                         : mSkipChars->mRanges[0].Start();
      *aRunLength = end - mOriginalStringOffset;
    }
    return mSkipChars->mCharCount == uint32_t(mOriginalStringOffset);
  }

  const gfxSkipChars::SkippedRange& range =
      mSkipChars->mRanges[mCurrentRangeIndex];

  if (uint32_t(mOriginalStringOffset) < range.End()) {
    if (aRunLength) {
      *aRunLength = range.End() - mOriginalStringOffset;
    }
    return true;
  }

  if (aRunLength) {
    uint32_t end =
        uint32_t(mCurrentRangeIndex) + 1 < mSkipChars->mRanges.Length()
            ? mSkipChars->mRanges[mCurrentRangeIndex + 1].Start()
            : mSkipChars->mCharCount;
    *aRunLength = end - mOriginalStringOffset;
  }

  return mSkipChars->mCharCount == uint32_t(mOriginalStringOffset);
}
