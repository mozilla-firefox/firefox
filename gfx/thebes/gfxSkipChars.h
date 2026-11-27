/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_SKIP_CHARS_H
#define GFX_SKIP_CHARS_H

#include "nsTArray.h"

/*
 * gfxSkipChars is a data structure representing a list of characters that
 * have been skipped. The initial string is called the "original string"
 * and after skipping some characters, the result is called the "skipped
 * string". gfxSkipChars provides efficient ways to translate between offsets in
 * the original string and the skipped string. It is used by textrun code to
 * keep track of offsets before and after text transformations such as
 * whitespace compression and control code deletion.
 */

/**
 * The gfxSkipChars is represented as a sorted array of skipped ranges.
 *
 * A freshly-created gfxSkipChars means "all chars kept".
 */
class gfxSkipChars {
  friend struct SkippedRangeStartComparator;
  friend struct SkippedRangeOffsetComparator;

 private:
  class SkippedRange {
   public:
    SkippedRange(uint32_t aOffset, uint32_t aLength, uint32_t aDelta)
        : mOffset(aOffset), mLength(aLength), mDelta(aDelta) {}

    uint32_t Start() const { return mOffset; }

    uint32_t End() const { return mOffset + mLength; }

    uint32_t Length() const { return mLength; }

    uint32_t SkippedOffset() const { return mOffset - mDelta; }

    uint32_t Delta() const { return mDelta; }

    uint32_t NextDelta() const { return mDelta + mLength; }

    void Extend(uint32_t aChars) { mLength += aChars; }

   private:
    uint32_t mOffset;  // original-string offset at which we want to skip
    uint32_t mLength;  // number of skipped chars at this offset
    uint32_t mDelta;   // sum of lengths of preceding skipped-ranges
  };

 public:
  gfxSkipChars() : mCharCount(0) {}

  void SkipChars(uint32_t aChars) {
    NS_ASSERTION(mCharCount + aChars > mCharCount, "Character count overflow");
    uint32_t rangeCount = mRanges.Length();
    uint32_t delta = 0;
    if (rangeCount > 0) {
      SkippedRange& lastRange = mRanges[rangeCount - 1];
      if (lastRange.End() == mCharCount) {
        lastRange.Extend(aChars);
        mCharCount += aChars;
        return;
      }
      delta = lastRange.NextDelta();
    }
    mRanges.AppendElement(SkippedRange(mCharCount, aChars, delta));
    mCharCount += aChars;
  }

  void KeepChars(uint32_t aChars) {
    NS_ASSERTION(mCharCount + aChars > mCharCount, "Character count overflow");
    mCharCount += aChars;
  }

  void SkipChar() { SkipChars(1); }

  void KeepChar() { KeepChars(1); }

  void TakeFrom(gfxSkipChars* aSkipChars) {
    mRanges = std::move(aSkipChars->mRanges);
    mCharCount = aSkipChars->mCharCount;
    aSkipChars->mCharCount = 0;
  }

  int32_t GetOriginalCharCount() const { return mCharCount; }

  const SkippedRange& LastRange() const {
    // this is only valid if mRanges is non-empty; no assertion here
    // because nsTArray will already assert if we abuse it
    return mRanges[mRanges.Length() - 1];
  }

  friend class gfxSkipCharsIterator;

 private:
  nsTArray<SkippedRange> mRanges;
  uint32_t mCharCount;
};

/**
 * A gfxSkipCharsIterator represents a position in the original string. It lets
 * you map efficiently to and from positions in the string after skipped
 * characters have been removed. You can also specify an offset that is added to
 * all incoming original string offsets and subtracted from all outgoing
 * original string offsets --- useful when the gfxSkipChars corresponds to
 * something offset from the original DOM coordinates, which it often does for
 * gfxTextRuns.
 *
 * The current positions (in both the original and skipped strings) are
 * always constrained to be >= 0 and <= the string length. When the position
 * is equal to the string length, it is at the end of the string. The current
 * positions do not include any aOriginalStringToSkipCharsOffset.
 *
 * When the position in the original string corresponds to a skipped character,
 * the skipped-characters offset is the offset of the next unskipped character,
 * or the skipped-characters string length if there is no next unskipped
 * character.
 */
class gfxSkipCharsIterator {
 public:
  /**
   * @param aOriginalStringToSkipCharsOffset add this to all incoming and
   * outgoing original string offsets
   */
  gfxSkipCharsIterator(const gfxSkipChars& aSkipChars,
                       int32_t aOriginalStringToSkipCharsOffset,
                       int32_t aOriginalStringOffset)
      : mSkipChars(&aSkipChars),
        mOriginalStringOffset(0),
        mSkippedStringOffset(0),
        mCurrentRangeIndex(-1),
        mOriginalStringToSkipCharsOffset(aOriginalStringToSkipCharsOffset) {
    SetOriginalOffset(aOriginalStringOffset);
  }

  explicit gfxSkipCharsIterator(const gfxSkipChars& aSkipChars,
                                int32_t aOriginalStringToSkipCharsOffset = 0)
      : mSkipChars(&aSkipChars),
        mOriginalStringOffset(0),
        mSkippedStringOffset(0),
        mOriginalStringToSkipCharsOffset(aOriginalStringToSkipCharsOffset) {
    mCurrentRangeIndex =
        mSkipChars->mRanges.IsEmpty() || mSkipChars->mRanges[0].Start() > 0 ? -1
                                                                            : 0;
  }

  gfxSkipCharsIterator(const gfxSkipCharsIterator& aIterator) = default;

  /**
   * The empty constructor creates an object that is useless until it is
   * assigned.
   */
  gfxSkipCharsIterator()
      : mSkipChars(nullptr),
        mOriginalStringOffset(0),
        mSkippedStringOffset(0),
        mCurrentRangeIndex(0),
        mOriginalStringToSkipCharsOffset(0) {}

  /**
   * Return true if this iterator is properly initialized and usable.
   */
  bool IsInitialized() const { return mSkipChars != nullptr; }

  /**
   * Set the iterator to aOriginalStringOffset in the original string.
   * This can efficiently move forward or backward from the current position.
   * aOriginalStringOffset is clamped to [0,originalStringLength].
   */
  void SetOriginalOffset(int32_t aOriginalStringOffset);

  /**
   * Set the iterator to aSkippedStringOffset in the skipped string.
   * This can efficiently move forward or backward from the current position.
   * aSkippedStringOffset is clamped to [0,skippedStringLength].
   */
  void SetSkippedOffset(uint32_t aSkippedStringOffset);

  uint32_t ConvertOriginalToSkipped(int32_t aOriginalStringOffset) {
    SetOriginalOffset(aOriginalStringOffset);
    return GetSkippedOffset();
  }

  int32_t ConvertSkippedToOriginal(uint32_t aSkippedStringOffset) {
    SetSkippedOffset(aSkippedStringOffset);
    return GetOriginalOffset();
  }

  /**
   * Test if the character at the current position in the original string
   * is skipped or not. If aRunLength is non-null, then *aRunLength is set
   * to a number of characters all of which are either skipped or not, starting
   * at this character. When the current position is at the end of the original
   * string, we return true and *aRunLength is set to zero.
   */
  bool IsOriginalCharSkipped(int32_t* aRunLength = nullptr) const;

  void AdvanceOriginal(int32_t aDelta) {
    SetOriginalOffset(GetOriginalOffset() + aDelta);
  }

  void AdvanceSkipped(int32_t aDelta) {
    SetSkippedOffset(GetSkippedOffset() + aDelta);
  }

  /**
   * @return the offset within the original string
   */
  int32_t GetOriginalOffset() const {
    return mOriginalStringOffset - mOriginalStringToSkipCharsOffset;
  }

  /**
   * @return the offset within the skipped string corresponding to the
   * current position in the original string. If the current position
   * in the original string is a character that is skipped, then we return
   * the position corresponding to the first non-skipped character in the
   * original string after the current position, or the length of the skipped
   * string if there is no such character.
   */
  uint32_t GetSkippedOffset() const { return mSkippedStringOffset; }

  int32_t GetOriginalEnd() const {
    return mSkipChars->GetOriginalCharCount() -
           mOriginalStringToSkipCharsOffset;
  }

 private:
  const gfxSkipChars* mSkipChars;

  // Current position
  int32_t mOriginalStringOffset;
  uint32_t mSkippedStringOffset;

  // Index of the last skippedRange that precedes or contains the current
  // position in the original string.
  // If index == -1 then we are before the first skipped char.
  int32_t mCurrentRangeIndex;

  // This offset is added to map from "skipped+unskipped characters in
  // the original DOM string" character space to "skipped+unskipped
  // characters in the textrun's gfxSkipChars" character space
  int32_t mOriginalStringToSkipCharsOffset;
};

#endif /*GFX_SKIP_CHARS_H*/
